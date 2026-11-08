// SPDX-License-Identifier: GPL-3.0-only
#include "UpgradePlanTask.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include "Application.h"
#include "Json.h"
#include "Version.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/WorldList.h"
#include "modplatform/ModIndex.h"
#include "modplatform/helpers/HashUtils.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameModIndex.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/ApiRequest.h"
#include "net/RPCSink.h"
#include "qmlui/resources/ResourceCommon.h"
#include "tasks/ConcurrentTask.h"

namespace {

/// Same table as LauncherBridge / CreateBridge use for loader keys and component uids.
struct LoaderUid {
    const char* key;
    const char* uid;
    const char* name;
};
const LoaderUid kLoaders[] = {
    { "fabric", "net.fabricmc.fabric-loader", "Fabric" },
    { "quilt", "org.quiltmc.quilt-loader", "Quilt" },
    { "forge", "net.minecraftforge", "Forge" },
    { "neoforge", "net.neoforged", "NeoForge" },
};

}  // namespace

UpgradePlanTask::UpgradePlanTask(QString instanceId, QString targetMcVersion)
    : Task(), m_instanceId(std::move(instanceId)), m_targetMc(std::move(targetMcVersion))
{}

UpgradePlanTask::~UpgradePlanTask()
{
    for (const auto& sub : m_subs) {
        sub->disconnect(this);
    }
}

bool UpgradePlanTask::abort()
{
    m_aborted = true;
    for (const auto& sub : m_subs) {
        if (sub) {
            sub->abort();
        }
    }
    return Task::abort();
}

UpgradeRow* UpgradePlanTask::rowById(const QString& id)
{
    for (auto& row : m_rows) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

void UpgradePlanTask::setSub(const Task::Ptr& sub)
{
    m_subs.append(sub);
}

void UpgradePlanTask::onSubFinished(const Task::Ptr& sub, std::function<void(bool ok, const QString& error)> then)
{
    connect(sub.get(), &Task::finished, this, [this, sub, then] {
        const bool ok = sub->wasSuccessful();
        const QString error = sub->failReason();
        m_subs.removeAll(sub);
        if (m_aborted || isFinished()) {
            return;
        }
        then(ok, error);
    });
}

void UpgradePlanTask::executeTask()
{
    auto* inst = QmlResources::findInstance(m_instanceId);
    if (!inst) {
        emitFailed(tr("This instance no longer exists."));
        return;
    }
    if (m_targetMc.isEmpty()) {
        emitFailed(tr("Pick a Minecraft version first."));
        return;
    }
    m_inst = inst;
    m_currentMc = inst->getPackProfile()->getComponentVersion("net.minecraft");
    const QDir worldsDir(m_inst->worldList()->dir().absolutePath());
    m_hasWorlds = !worldsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
    scanFolder();
}

// ---- phase 1: list the mods and their metadata (nothing on disk is touched) -------------------

void UpgradePlanTask::scanFolder()
{
    setStatus(tr("Looking at the installed mods…"));
    auto* folder = m_inst->loaderModList();
    if (!folder) {
        emitFailed(tr("Couldn't open the mods folder of this instance."));
        return;
    }
    m_folder = folder;
    // The folder scan is asynchronous; updateFinished fires when it is done (also when a scan
    // started by another model of the same folder is still in flight).
    connect(folder, &ResourceFolderModel::updateFinished, this, &UpgradePlanTask::rowsScanned, Qt::UniqueConnection);
    if (!folder->update() && !folder->empty()) {
        rowsScanned();  // the rows are already there (the folder was scanned before)
    }
    // Safety net: a stuck scan must not hang the plan forever.
    QTimer::singleShot(30000, this, [this] {
        if (!isFinished() && m_folder && !m_folder->empty()) {
            rowsScanned();
        }
    });
}

void UpgradePlanTask::rowsScanned()
{
    if (m_aborted || isFinished() || !m_folder) {
        return;
    }
    disconnect(m_folder, &ResourceFolderModel::updateFinished, this, &UpgradePlanTask::rowsScanned);
    m_rows.clear();
    for (auto* res : m_folder->allResources()) {
        UpgradeRow row;
        row.fileName = res->fileinfo().fileName();
        row.id = row.fileName;
        row.name = res->name();
        row.enabled = res->enabled();
        row.currentVersion = res->version();
        row.isDir = res->type() == ResourceType::FOLDER || res->fileinfo().isDir();
        if (auto meta = res->metadata()) {
            row.slug = meta->slug;
            row.provider = meta->provider == ModPlatform::ResourceProvider::FLAME ? "curseforge" : "modrinth";
            row.hash = meta->hash;
            row.hashFormat = meta->hashFormat;
            row.modLoaders = meta->loaders;
            if (!meta->versionNumber.isEmpty()) {
                row.currentVersion = meta->versionNumber;
            }
            if (!row.isDir) {
                row.pack = std::make_shared<ModPlatform::IndexedPack>();
                row.pack->name = row.name;
                row.pack->slug = meta->slug;
                row.pack->addonId = meta->projectId;
                row.pack->provider = meta->provider;
            }
        }
        row.status = UpgradeLogic::classify(!row.isDir && row.pack != nullptr, false);
        m_rows.append(row);
    }
    checkLoader();
}

// ---- phase 2: does the mod loader itself exist for the target Minecraft version? --------------

void UpgradePlanTask::checkLoader()
{
    setStatus(tr("Checking the mod loader…"));
    m_loader = {};
    auto* profile = m_inst->getPackProfile();
    const auto installed = profile->getModLoadersList();
    if (!installed.isEmpty()) {
        const QString key = ModPlatform::getModLoaderAsString(installed.first());
        for (const auto& l : kLoaders) {
            if (key == QLatin1String(l.key)) {
                m_loader.key = l.key;
                m_loader.name = QString::fromLatin1(l.name);
                m_loader.uid = QString::fromLatin1(l.uid);
                m_loader.currentVersion = profile->getComponentVersion(m_loader.uid);
                break;
            }
        }
        if (m_loader.key.isEmpty()) {
            // An exotic loader (Legacy Fabric, ...): keep it, but its availability can't be checked here.
            m_loader.key = key;
            m_loader.name = key;
            m_loader.ok = true;
            m_loader.checked = false;
            return loaderChecked();
        }
    }

    const auto kind = UpgradeLogic::loader(m_loader.key);
    if (kind == UpgradeLogic::Loader::None) {
        m_loader.ok = true;
        m_loader.checked = true;
        return loaderChecked();
    }
    if (UpgradeLogic::loaderVersionIndependent(kind)) {
        m_loader.ok = UpgradeLogic::loaderSupportsMc(kind, m_targetMc);
        m_loader.checked = true;
        m_loader.newVersion = m_loader.currentVersion;  // loader builds are Minecraft-independent
        return loaderChecked();
    }

    // Forge / NeoForge: loader builds are per-Minecraft; ask the metadata list for one.
    auto list = APPLICATION->metadataIndex()->get(m_loader.uid);
    if (!list) {
        m_loader.ok = false;
        m_loader.checked = true;
        return loaderChecked();
    }
    auto evaluate = [this, list]() {
        QString best;
        qint64 bestTime = -1;
        QString bestRecommended;
        qint64 bestRecommendedTime = -1;
        for (const auto& v : list->versions()) {
            const auto& reqs = v->requiredSet();
            auto it = std::find_if(reqs.begin(), reqs.end(), [](const Meta::Require& r) { return r.uid == "net.minecraft"; });
            if (it == reqs.end() || it->equalsVersion != m_targetMc) {
                continue;
            }
            if (v->rawTime() > bestTime) {
                bestTime = v->rawTime();
                best = v->version();
            }
            if (v->isRecommended() && v->rawTime() > bestRecommendedTime) {
                bestRecommendedTime = v->rawTime();
                bestRecommended = v->version();
            }
        }
        m_loader.newVersion = !bestRecommended.isEmpty() ? bestRecommended : best;
        m_loader.ok = !m_loader.newVersion.isEmpty();
        m_loader.checked = true;
        loaderChecked();
    };
    if (list->isLoaded()) {
        evaluate();
        return;
    }
    auto task = list->getLoadTask();
    if (!task) {
        m_loader.ok = false;
        m_loader.checked = true;
        return loaderChecked();
    }
    setSub(task);
    onSubFinished(task, [this, evaluate](bool ok, const QString& error) {
        if (!ok) {
            emitFailed(tr("Couldn't load the %1 version list: %2").arg(m_loader.name, error));
            return;
        }
        evaluate();
    });
    task->start();
}

void UpgradePlanTask::loaderChecked()
{
    if (m_aborted || isFinished()) {
        return;
    }
    queryModrinth();
}

// ---- phase 3: one batched Modrinth call for every mod it knows by hash ------------------------

void UpgradePlanTask::queryModrinth()
{
    const QString sha1 = ModPlatform::ProviderCapabilities::hashType(ModPlatform::ResourceProvider::MODRINTH).first();

    QList<UpgradeRow*> rehash;
    for (auto& row : m_rows) {
        if (row.provider != "modrinth" || !row.pack) {
            continue;
        }
        if (row.hashFormat == sha1 && !row.hash.isEmpty()) {
            m_hashesByRow.insert(row.id, row.hash);
        } else {
            rehash << &row;
        }
    }

    auto askModrinth = [this, sha1] {
        if (m_hashesByRow.isEmpty()) {
            return queryFlame();
        }
        setStatus(tr("Asking Modrinth about %n mod(s)…", "", m_hashesByRow.size()));
        auto [job, response] = ModrinthAPI::get().latestVersions(m_hashesByRow.values(), sha1,
                                                                 std::vector<Version>{ Version(m_targetMc) },
                                                                 m_inst->getPackProfile()->getModLoaders());
        setSub(job);
        onSubFinished(job, [this, response, sha1](bool ok, const QString& error) {
            if (!ok) {
                emitFailed(tr("Modrinth did not answer: %1").arg(error));
                return;
            }
            QString loaderFilter;
            const auto installed = m_inst->getPackProfile()->getModLoadersList();
            if (!installed.isEmpty()) {
                loaderFilter = ModPlatform::getModLoaderAsString(installed.first());
            }
            const auto doc = QJsonDocument::fromJson(*response);
            for (auto& row : m_rows) {
                const QString hash = m_hashesByRow.value(row.id);
                if (hash.isEmpty()) {
                    continue;
                }
                const QJsonObject obj = doc.object().value(hash).toObject();
                if (obj.isEmpty()) {
                    continue;  // no version for this Minecraft/loader combination -> stays "missing"
                }
                auto parsed = Modrinth::loadIndexedPackVersion(obj, sha1, loaderFilter);
                if (!parsed) {
                    continue;
                }
                auto version = parsed.value();
                if (version.downloadUrl.isEmpty()) {
                    continue;
                }
                row.target = version;
                row.targetVersion = version.versionNumber;
                row.status = UpgradeLogic::classify(true, true);
            }
            queryFlame();
        });
        job->start();
    };

    if (rehash.isEmpty()) {
        return askModrinth();
    }

    // Rare: the metadata carries a different hash type than Modrinth prefers — recompute sha1.
    setStatus(tr("Hashing %n mod(s)…", "", rehash.size()));
    auto hashing = makeShared<ConcurrentTask>("UpgradeHashes", APPLICATION->settings()->get("NumberOfConcurrentTasks").toInt());
    for (auto* row : rehash) {
        auto hasher = Hashing::createHasher(m_folder->dir().absoluteFilePath(row->fileName), ModPlatform::ResourceProvider::MODRINTH);
        connect(hasher.get(), &Hashing::Hasher::resultsReady, this,
                [this, id = row->id](const QString& hash) { m_hashesByRow.insert(id, hash); });
        hashing->addTask(hasher);
    }
    setSub(hashing);
    onSubFinished(hashing, [this, askModrinth](bool, const QString&) { askModrinth(); });
    hashing->start();
}

// ---- phase 4: CurseForge, one files request per mod with a known id ---------------------------

void UpgradePlanTask::queryFlame()
{
    QList<UpgradeRow*> flameRows;
    for (auto& row : m_rows) {
        if (row.provider == "curseforge" && row.pack) {
            flameRows << &row;
        }
    }
    if (flameRows.isEmpty()) {
        return fetchIcons();
    }
    setStatus(tr("Asking CurseForge about %n mod(s)…", "", flameRows.size()));
    auto* job = new NetJob("UpgradePlan::FlameVersions", APPLICATION->network());
    const auto loaders = m_inst->getPackProfile()->getModLoadersList();
    for (auto* row : flameRows) {
        auto pack = row->pack;
        auto url = FlameAPI::get().getVersionsURL({ .pack = pack, .mcVersions = std::vector<Version>{ Version(m_targetMc) } });
        if (!url.has_value()) {
            continue;
        }
        auto [task, response] = Net::ApiRequest::makeByteArray(url.value());
        connect(task.get(), &Task::succeeded, this, [this, pack, response, rowId = row->id, loaders] {
            if (m_aborted) {
                return;
            }
            auto versions = Json::requireArray(*response, "data");
            if (!versions) {
                qWarning() << "UpgradePlan: couldn't parse the CurseForge answer for" << rowId << ":" << versions.error();
                return;
            }
            auto* row = rowById(rowId);
            if (!row) {
                return;
            }
            if (auto parsed = FlameMod::loadIndexedPackVersions(*pack, versions.value()); !parsed) {
                qWarning() << "UpgradePlan: couldn't parse the CurseForge versions for" << rowId << ":" << parsed.error();
                return;
            }
            const auto latest = FlameAPI::getLatestVersion(pack->versions, loaders, row->modLoaders, !loaders.isEmpty(), {});
            if (!latest.has_value() || !latest->addonId.isValid() || latest->downloadUrl.isEmpty()) {
                return;  // no version for this Minecraft/loader combination
            }
            row->target = *latest;
            row->targetVersion = latest->version;
            row->status = UpgradeLogic::classify(true, true);
        });
        job->addNetAction(task);
    }
    Task::Ptr jobPtr(job);
    setSub(jobPtr);
    onSubFinished(jobPtr, [this](bool, const QString&) { fetchIcons(); });
    job->start();
}

// ---- phase 5: project icons (one batched request per provider) --------------------------------

void UpgradePlanTask::fetchIcons()
{
    QStringList modrinthIds, flameIds;
    for (const auto& row : m_rows) {
        if (!row.pack) {
            continue;
        }
        (row.provider == "curseforge" ? flameIds : modrinthIds) << row.pack->addonId.toString();
    }
    m_iconsWaiting = (modrinthIds.isEmpty() ? 0 : 1) + (flameIds.isEmpty() ? 0 : 1);
    if (m_iconsWaiting == 0) {
        return searchAlternatives();
    }
    setStatus(tr("Fetching mod icons…"));
    if (!modrinthIds.isEmpty()) {
        auto [job, response] = ModrinthAPI::get().getProjects(modrinthIds);
        setSub(job);
        onSubFinished(job, [this, response](bool, const QString&) {
            const auto arr = QJsonDocument::fromJson(*response).array();
            for (const auto& v : arr) {
                const auto o = v.toObject();
                const QString id = o.value("id").toString();
                const QString icon = o.value("icon_url").toString();
                if (id.isEmpty() || icon.isEmpty()) {
                    continue;
                }
                for (auto& row : m_rows) {
                    if (row.pack && row.provider != "curseforge" && row.pack->addonId.toString() == id) {
                        row.icon = icon;
                    }
                }
            }
            if (--m_iconsWaiting <= 0) {
                searchAlternatives();
            }
        });
        job->start();
    }
    if (!flameIds.isEmpty()) {
        auto [job, response] = FlameAPI::get().getProjects(flameIds);
        setSub(job);
        onSubFinished(job, [this, response](bool, const QString&) {
            const auto arr = QJsonDocument::fromJson(*response).object().value("data").toArray();
            for (const auto& v : arr) {
                const auto o = v.toObject();
                const QString id = QString::number(o.value("id").toInteger());
                const auto logo = o.value("logo").toObject();
                const QString icon = logo.value("thumbnailUrl").toString(logo.value("url").toString());
                if (id.isEmpty() || icon.isEmpty()) {
                    continue;
                }
                for (auto& row : m_rows) {
                    if (row.pack && row.provider == "curseforge" && row.pack->addonId.toString() == id) {
                        row.icon = icon;
                    }
                }
            }
            if (--m_iconsWaiting <= 0) {
                searchAlternatives();
            }
        });
        job->start();
    }
}

// ---- phase 6: for missing mods, search Modrinth for alternatives ------------------------------

void UpgradePlanTask::searchAlternatives()
{
    QList<UpgradeRow*> missing;
    for (auto& row : m_rows) {
        if (row.status == UpgradeLogic::Status::Missing) {
            missing << &row;
        }
    }
    if (missing.isEmpty()) {
        return finish();
    }
    setStatus(tr("Looking for alternatives…"));

    // Each search action carries its own response buffer, matched back to its row here.
    struct SearchOutcome {
        QString rowId;
        QList<ModPlatform::IndexedPack>* response = nullptr;
    };
    auto outcomes = std::make_shared<QList<SearchOutcome>>();
    auto* job = new NetJob("UpgradePlan::Search", APPLICATION->network());

    ResourceAPI::SearchArgs base;
    base.type = ModPlatform::ResourceType::Mod;
    base.offset = 0;
    if (auto loaders = m_inst->getPackProfile()->getModLoaders()) {
        base.loaders = loaders;
    }
    base.versions = std::vector<Version>{ Version(m_targetMc) };

    for (auto* row : missing) {
        ResourceAPI::SearchArgs args = base;
        args.search = row->name;
        auto spec = ModrinthAPI::get().searchProjects(args);
        auto [action, response] = Net::RPC::make<QList<ModPlatform::IndexedPack>>(spec);
        if (!action) {
            continue;
        }
        outcomes->append({ row->id, response });
        connect(action.get(), &Task::succeeded, this, [this, outcomes, rowId = row->id] {
            auto* r = rowById(rowId);
            if (!r || !r->pack) {
                return;
            }
            for (const auto& o : *outcomes) {
                if (o.rowId != rowId) {
                    continue;
                }
                int added = 0;
                for (const auto& pack : *o.response) {
                    if (pack.addonId.toString() == r->pack->addonId.toString()) {
                        continue;  // the mod itself is not a suggestion
                    }
                    UpgradeAlt alt;
                    alt.projectId = pack.addonId.toString();
                    alt.name = pack.name;
                    alt.iconUrl = pack.logoUrl;
                    alt.summary = pack.description;
                    alt.pack = std::make_shared<ModPlatform::IndexedPack>(pack);
                    r->alts.append(alt);
                    if (++added >= 2) {
                        break;
                    }
                }
                break;
            }
        });
        job->addNetAction(action);
    }
    Task::Ptr jobPtr(job);
    setSub(jobPtr);
    onSubFinished(jobPtr, [this](bool, const QString&) { fetchAltVersions(); });
    job->start();
}

// Each suggestion needs a concrete downloadable version for the target Minecraft + loader.

void UpgradePlanTask::fetchAltVersions()
{
    int pending = 0;
    auto downloads =
        makeShared<ConcurrentTask>("UpgradePlan::AltVersions", APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    for (auto& row : m_rows) {
        if (row.status != UpgradeLogic::Status::Missing) {
            continue;
        }
        for (int i = 0; i < row.alts.size(); ++i) {
            auto alt = std::make_shared<UpgradeAlt>(row.alts.at(i));  // copied back once the lookup lands
            ResourceAPI::VersionSearchArgs args;
            args.pack = alt->pack;
            args.resourceType = ModPlatform::ResourceType::Mod;
            args.mcVersions = std::vector<Version>{ Version(m_targetMc) };
            if (auto loaders = m_inst->getPackProfile()->getModLoaders()) {
                args.loaders = loaders;
            }
            ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> cb;
            cb.onSucceed = [alt](QVector<ModPlatform::IndexedVersion>& list) {
                if (!list.isEmpty() && !list.first().downloadUrl.isEmpty()) {
                    alt->version = list.first();
                    alt->hasVersion = true;
                }
            };
            auto task = ModrinthAPI::get().getProjectVersions(args, cb);
            if (!task) {
                continue;
            }
            ++pending;
            connect(task.get(), &Task::finished, this, [this, alt, rowId = row.id, i] {
                if (auto* r = rowById(rowId); r && i < r->alts.size()) {
                    r->alts[i] = *alt;
                }
            });
            downloads->addTask(task);
        }
    }
    if (pending == 0) {
        return finish();
    }
    setSub(downloads);
    onSubFinished(downloads, [this](bool, const QString&) { finish(); });
    downloads->start();
}

void UpgradePlanTask::finish()
{
    if (m_aborted || isFinished()) {
        return;
    }
    // Suggestions without a downloadable version are noise: drop them.
    for (auto& row : m_rows) {
        row.alts.removeIf([](const UpgradeAlt& a) { return !a.hasVersion; });
        // Mods with metadata but no answer anywhere are "missing", the rest stay "unknown".
        if (row.status != UpgradeLogic::Status::Ready && !row.pack) {
            row.status = UpgradeLogic::Status::Unknown;
        }
    }
    emitSucceeded();
}
