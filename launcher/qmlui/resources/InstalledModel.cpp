// SPDX-License-Identifier: GPL-3.0-only
#include "InstalledModel.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QRegularExpression>

#include "Application.h"
#include "DesktopServices.h"
#include "ResourceCommon.h"
#include "ResourceDownloadTask.h"
#include "qmlui/jobs/TaskCenter.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/DataPack.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
#include "modplatform/EnsureMetadataTask.h"
#include "modplatform/flame/FlameCheckUpdate.h"
#include "modplatform/modrinth/ModrinthCheckUpdate.h"
#include "settings/SettingsObject.h"
#include "tasks/ConcurrentTask.h"
#include "tasks/SequentialTask.h"

InstalledModel::InstalledModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    connect(this, &QAbstractItemModel::rowsInserted, this, &InstalledModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &InstalledModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &InstalledModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &InstalledModel::countChanged);
}

InstalledModel::~InstalledModel()
{
    if (m_task) {
        m_task->disconnect(this);
    }
    detach();
}

QHash<int, QByteArray> InstalledModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { VersionRole, "version" },
        { AuthorRole, "author" },
        { FileNameRole, "fileName" },
        { EnabledRole, "enabled" },
        { IconRole, "icon" },
        { DescriptionRole, "description" },
        { ProviderRole, "provider" },
        { HomepageRole, "homepage" },
        { NewVersionRole, "newVersion" },
        { IssuesRole, "issues" },
        { IsFolderRole, "isFolder" },
    };
}

void InstalledModel::setInstanceId(const QString& id)
{
    if (id == m_instanceId) {
        return;
    }
    m_instanceId = id;
    attach();
}

void InstalledModel::setKind(const QString& kind)
{
    if (kind == m_kind) {
        return;
    }
    m_kind = kind;
    attach();
}

void InstalledModel::setFilter(const QString& f)
{
    if (f == m_filter) {
        return;
    }
    m_filter = f;
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}

void InstalledModel::detach()
{
    if (m_folder) {
        m_folder->disconnect(this);
        m_folder->stopWatching();
    }
    m_folder = nullptr;
    setSourceModel(nullptr);
}

void InstalledModel::attach()
{
    cancel();
    detach();
    clearUpdates();
    m_instance = QmlResources::findInstance(m_instanceId);
    m_folder = QmlResources::folderFor(m_instance, m_kind);
    if (m_folder) {
        setSourceModel(m_folder);
        sort(0);
        m_loading = true;
        connect(m_folder, &ResourceFolderModel::updateFinished, this, [this] {
            if (m_loading) {
                m_loading = false;
                emit loadingChanged();
            }
            emit countChanged();
        });
        m_folder->startWatching();
        m_folder->update();
    } else {
        m_loading = false;
    }
    emit loadingChanged();
    emit sourceChanged();
    emit countChanged();
}

int InstalledModel::totalCount() const
{
    return m_folder ? static_cast<int>(m_folder->size()) : 0;
}

QString InstalledModel::folderPath() const
{
    return m_folder ? m_folder->dir().absolutePath() : QString();
}

bool InstalledModel::hasModLoader() const
{
    return QmlResources::hasModLoader(m_instance);
}

bool InstalledModel::filterAcceptsRow(int sourceRow, const QModelIndex& /*sourceParent*/) const
{
    if (!m_folder || sourceRow >= m_folder->size()) {
        return false;
    }
    if (m_filter.isEmpty()) {
        return true;
    }
    const QRegularExpression re(QRegularExpression::escape(m_filter), QRegularExpression::CaseInsensitiveOption);
    const Resource& res = m_folder->at(sourceRow);
    if (res.applyFilter(re)) {
        return true;
    }
    if (const auto* mod = dynamic_cast<const Mod*>(&res)) {
        return re.match(mod->authors().join(' ')).hasMatch();
    }
    return false;
}

bool InstalledModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    if (!m_folder) {
        return false;
    }
    return m_folder->at(left.row()).name().compare(m_folder->at(right.row()).name(), Qt::CaseInsensitive) < 0;
}

Resource* InstalledModel::resourceAt(int proxyRow) const
{
    if (!m_folder || proxyRow < 0 || proxyRow >= rowCount()) {
        return nullptr;
    }
    const int src = mapToSource(index(proxyRow, 0)).row();
    if (src < 0 || src >= m_folder->size()) {
        return nullptr;
    }
    return &m_folder->at(src);
}

QVariant InstalledModel::data(const QModelIndex& idx, int role) const
{
    if (role < NameRole) {
        return QSortFilterProxyModel::data(idx, role);
    }
    Resource* res = resourceAt(idx.row());
    if (!res) {
        return {};
    }
    const auto* mod = dynamic_cast<const Mod*>(res);
    const auto* pack = dynamic_cast<const DataPack*>(res);
    switch (role) {
        case NameRole:
            return res->name();
        case VersionRole:
            return res->version();
        case AuthorRole:
            return mod ? mod->authors().join(", ") : QString();
        case FileNameRole:
            return res->fileinfo().fileName();
        case EnabledRole:
            return res->enabled();
        case IconRole: {
            // Only hand out a URL once there is a picture, so the delegate can fall back to a generic icon.
            bool has = false;
            if (mod) {
                has = !mod->icon(QSize(64, 64), Qt::KeepAspectRatio).isNull();
            } else if (pack) {
                has = !pack->image(QSize(64, 64), Qt::KeepAspectRatio).isNull();
            }
            if (!has) {
                return QString();
            }
            return QString("image://resicon/%1/%2/%3")
                .arg(m_instanceId, m_kind, QString::fromLatin1(QUrl::toPercentEncoding(res->internalId())));
        }
        case DescriptionRole:
            if (mod) {
                return mod->description();
            }
            return pack ? pack->description() : QString();
        case ProviderRole:
            return res->metadata() ? res->provider() : QString();
        case HomepageRole:
            return res->homepage();
        case NewVersionRole:
            return m_newVersionById.value(res->internalId());
        case IssuesRole:
            return res->issues().join("\n");
        case IsFolderRole:
            return res->fileinfo().isDir();
        default:
            return {};
    }
}

void InstalledModel::setItemEnabled(int row, bool enabled)
{
    if (!m_folder || row < 0 || row >= rowCount()) {
        return;
    }
    const QModelIndex src = mapToSource(index(row, 0));
    m_folder->setResourceEnabled({ src }, enabled ? EnableAction::ENABLE : EnableAction::DISABLE);
}

void InstalledModel::remove(int row)
{
    if (!m_folder || row < 0 || row >= rowCount()) {
        return;
    }
    Resource* res = resourceAt(row);
    const QString name = res ? res->name() : QString();
    const QModelIndex src = mapToSource(index(row, 0));
    if (m_folder->deleteResources({ src })) {
        emit message(tr("Removed “%1”").arg(name), false);
    } else {
        emit message(tr("Could not remove “%1”").arg(name), true);
    }
}

void InstalledModel::openFolder() const
{
    if (m_folder) {
        DesktopServices::openPath(m_folder->dir().absolutePath(), true);
    }
}

void InstalledModel::openHomepage(int row) const
{
    if (Resource* res = resourceAt(row); res && !res->homepage().isEmpty()) {
        QDesktopServices::openUrl(QUrl(res->homepage()));
    }
}

int InstalledModel::addFiles(const QVariantList& urls)
{
    if (!m_folder) {
        return 0;
    }
    int added = 0;
    for (const auto& v : urls) {
        const QUrl url = v.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : v.toString();
        if (QFileInfo::exists(path) && m_folder->installResource(path)) {
            ++added;
        }
    }
    if (added > 0) {
        emit message(tr("Added %n file(s)", "", added), false);
    } else if (!urls.isEmpty()) {
        emit message(tr("These files could not be added"), true);
    }
    return added;
}

void InstalledModel::reload()
{
    if (m_folder) {
        m_folder->update();
    }
}

// ---- updates ---------------------------------------------------------------------------------

void InstalledModel::setBusy(const QString& b)
{
    if (b == m_busy) {
        return;
    }
    m_busy = b;
    if (b.isEmpty()) {
        m_progress = -1;
        m_statusText.clear();
        emit progressChanged();
    }
    emit busyChanged();
}

void InstalledModel::runTask(const Task::Ptr& task, std::function<void(bool ok, const QString& error)> done)
{
    m_task = task;
    m_progress = -1;
    emit progressChanged();
    connect(task.get(), &Task::progress, this, [this](qint64 cur, qint64 total) {
        m_progress = total > 0 ? double(cur) / double(total) : -1;
        emit progressChanged();
    });
    connect(task.get(), &Task::status, this, [this](const QString& s) {
        m_statusText = s;
        emit progressChanged();
    });
    auto weak = task.toWeakRef();
    connect(task.get(), &Task::finished, this, [this, weak, done] {
        auto t = weak.lock();
        const bool ok = t && t->wasSuccessful();
        const QString err = t ? t->failReason() : QString();
        if (t) {
            t->disconnect(this);
        }
        m_task.reset();
        done(ok, err);
    });
    TaskCenter::instance()->track(task, m_instance ? m_instance->name() : tr("Mods"), "resources", m_instanceId);
    task->start();
}

void InstalledModel::cancel()
{
    if (m_task) {
        auto t = m_task;
        t->abort();
    }
}

void InstalledModel::clearUpdates()
{
    m_updates.clear();
    m_newVersionById.clear();
    m_modrinthList.clear();
    m_flameList.clear();
    m_retryFlame.clear();
    m_modrinthCheck.reset();
    m_flameCheck.reset();
    const bool wasChecked = m_checked;
    m_checked = false;
    if (wasChecked && rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { NewVersionRole });
    }
    emit updatesChanged();
}

void InstalledModel::checkUpdates()
{
    if (!m_folder || !m_instance || !m_busy.isEmpty()) {
        return;
    }
    if (m_kind == "mods" && !hasModLoader()) {
        emit message(tr("Install a mod loader for this instance first"), true);
        return;
    }
    if (APPLICATION->settings()->get("ModMetadataDisabled").toBool()) {
        emit message(tr("Updates are unavailable because mod metadata is turned off in Settings"), true);
        return;
    }
    clearUpdates();
    setBusy("checking");

    // Resources without metadata get it first (Modrinth, then CurseForge for whatever Modrinth doesn't know).
    QList<Resource*> noMeta;
    for (auto* res : m_folder->allResources()) {
        if (res->status() == ResourceStatus::NoMetadata) {
            if (res->type() != ResourceType::FOLDER) {
                noMeta << res;
            }
        } else if (res->metadata()) {
            (res->metadata()->provider == ModPlatform::ResourceProvider::FLAME ? m_flameList : m_modrinthList) << res;
        }
    }
    if (noMeta.isEmpty()) {
        startChecks();
        return;
    }

    const QDir indexDir = m_folder->indexDir();
    auto seq = makeShared<SequentialTask>(tr("Looking for metadata"));
    auto mr = makeShared<EnsureMetadataTask>(noMeta, indexDir, ModPlatform::ResourceProvider::MODRINTH);
    connect(mr.get(), &EnsureMetadataTask::metadataReady, this, [this](Resource* r) { m_modrinthList << r; });
    connect(mr.get(), &EnsureMetadataTask::metadataFailed, this, [this](Resource* r) { m_retryFlame << r; });
    if (mr->getHashingTask()) {
        seq->addTask(mr->getHashingTask());
    }
    seq->addTask(mr);
    runTask(seq, [this, indexDir](bool, const QString&) {
        if (m_busy != "checking") {
            return;
        }
        if (m_retryFlame.isEmpty()) {
            startChecks();
            return;
        }
        auto seq2 = makeShared<SequentialTask>(tr("Looking for metadata"));
        auto cf = makeShared<EnsureMetadataTask>(m_retryFlame, indexDir, ModPlatform::ResourceProvider::FLAME);
        connect(cf.get(), &EnsureMetadataTask::metadataReady, this, [this](Resource* r) { m_flameList << r; });
        if (cf->getHashingTask()) {
            seq2->addTask(cf->getHashingTask());
        }
        seq2->addTask(cf);
        runTask(seq2, [this](bool, const QString&) {
            if (m_busy == "checking") {
                startChecks();
            }
        });
    });
}

void InstalledModel::startChecks()
{
    if (!m_folder || !m_instance) {
        setBusy("");
        return;
    }
    m_versions = { Version(QmlResources::gameVersion(m_instance)) };
    const auto loaders = m_instance->getPackProfile()->getModLoadersList();
    auto seq = makeShared<SequentialTask>(tr("Checking for updates"));
    if (!m_modrinthList.isEmpty()) {
        m_modrinthCheck.reset(new ModrinthCheckUpdate(m_modrinthList, m_versions, loaders, m_folder));
        seq->addTask(m_modrinthCheck);
    }
    if (!m_flameList.isEmpty()) {
        m_flameCheck.reset(new FlameCheckUpdate(m_flameList, m_versions, loaders, m_folder));
        seq->addTask(m_flameCheck);
    }
    if (!m_modrinthCheck && !m_flameCheck) {
        finishChecks();
        return;
    }
    runTask(seq, [this](bool ok, const QString& error) {
        if (m_busy != "checking") {
            return;
        }
        if (!ok && !error.isEmpty()) {
            emit message(error, true);
        }
        QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> deps;
        for (auto* check : { m_modrinthCheck.get(), m_flameCheck.get() }) {
            if (!check) {
                continue;
            }
            for (auto& u : check->getUpdates()) {
                if (u.download) {
                    m_updates.push_back(std::move(u));
                }
            }
            deps.append(check->getDependencies());
        }

        auto* mods = dynamic_cast<ModFolderModel*>(m_folder.data());
        if (!mods || deps.isEmpty() || m_updates.empty() || APPLICATION->settings()->get("ModDependenciesDisabled").toBool()) {
            finishChecks();
            return;
        }
        // New versions may need new dependencies; add those that aren't installed yet.
        auto depTask = makeShared<GetModDependenciesTask>(m_instance, mods, deps);
        auto weak = depTask.toWeakRef();
        runTask(depTask, [this, weak](bool, const QString&) {
            auto depTask = weak.lock();
            if (m_busy != "checking" || !depTask || !m_folder) {
                finishChecks();
                return;
            }
            const auto extra = depTask->getExtraInfo();
            for (const auto& dep : depTask->getDependecies()) {
                const auto info = extra.value(dep->version.addonId.toString());
                if (info.maybeInstalled) {
                    continue;
                }
                auto dl = makeShared<ResourceDownloadTask>(dep->pack, dep->version, m_folder, true, "dependency",
                                                           info.requiredByIds.value(0));
                m_updates.emplace_back(dep->pack->name, dep->version.hash, tr("Not installed"), dep->version.version,
                                       dep->version.versionType, dep->version.changelog, dep->pack->provider, dl, true);
            }
            finishChecks();
        });
    });
}

void InstalledModel::finishChecks()
{
    m_newVersionById.clear();
    if (m_folder) {
        for (const auto& u : m_updates) {
            for (auto* res : m_folder->allResources()) {
                auto meta = res->metadata();
                if (meta && (meta->hash == u.oldHash || meta->name == u.name)) {
                    m_newVersionById.insert(res->internalId(), u.newVersion);
                    break;
                }
            }
        }
    }
    m_modrinthCheck.reset();
    m_flameCheck.reset();
    m_checked = true;
    setBusy("");
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { NewVersionRole, ProviderRole });
    }
    emit updatesChanged();
    if (m_updates.empty()) {
        emit message(tr("Everything is up to date"), false);
    }
}

void InstalledModel::updateAll()
{
    if (m_updates.empty() || !m_busy.isEmpty() || !m_folder) {
        return;
    }
    if (m_instance && m_instance->isRunning()) {
        emit message(tr("Close the game before updating"), true);
        return;
    }
    auto tasks = makeShared<ConcurrentTask>(tr("Updating"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    const int n = static_cast<int>(m_updates.size());
    for (auto& u : m_updates) {
        tasks->addTask(u.download);
    }
    setBusy("updating");
    runTask(tasks, [this, n](bool ok, const QString& error) {
        setBusy("");
        clearUpdates();
        if (m_folder) {
            m_folder->update();
        }
        if (ok) {
            emit message(tr("Updated %n item(s)", "", n), false);
        } else if (!error.isEmpty()) {
            emit message(tr("Some updates failed: %1").arg(error), true);
        }
    });
}
