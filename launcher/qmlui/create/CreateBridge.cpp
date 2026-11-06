// SPDX-License-Identifier: GPL-3.0-only
#include "CreateBridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QUrl>
#include <QUrlQuery>

#include "Application.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "Json.h"
#include "MetaVersionModel.h"
#include "PackSearchModel.h"
#include "Version.h"
#include "icons/IconList.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "net/ApiRequest.h"
#include "settings/SettingsObject.h"
#include "tasks/SequentialTask.h"
#include "../tools/LauncherImport.h"
#include "../tools/ToolsBridge.h"

namespace {
struct LoaderInfo {
    const char* key;
    const char* uid;
    const char* name;
    bool fabricLike;  // the metadata doesn't tie these to a Minecraft version
};
constexpr LoaderInfo Loaders[] = {
    { "fabric", "net.fabricmc.fabric-loader", "Fabric", true },
    { "quilt", "org.quiltmc.quilt-loader", "Quilt", true },
    { "forge", "net.minecraftforge", "Forge", false },
    { "neoforge", "net.neoforged", "NeoForge", false },
};

const LoaderInfo* loaderInfo(const QString& key)
{
    for (const auto& l : Loaders) {
        if (key == QLatin1String(l.key)) {
            return &l;
        }
    }
    return nullptr;
}

QString shortDate(const QString& iso)
{
    const auto dt = QDateTime::fromString(iso, Qt::ISODate);
    return dt.isValid() ? QLocale().toString(dt.date(), QLocale::ShortFormat) : QString();
}
}  // namespace

CreateBridge::CreateBridge(QObject* parent)
    : QObject(parent)
    , m_versions(new MetaVersionModel(true, this))
    , m_loaderVersions(new MetaVersionModel(false, this))
    , m_packs(new PackSearchModel(this))
{}

CreateBridge::~CreateBridge() = default;

QObject* CreateBridge::versions() const
{
    return m_versions;
}
QObject* CreateBridge::loaderVersions() const
{
    return m_loaderVersions;
}
QObject* CreateBridge::packs() const
{
    return m_packs;
}

QString CreateBridge::loaderName() const
{
    const auto* info = loaderInfo(m_loader);
    return info ? QString::fromLatin1(info->name) : QString();
}

bool CreateBridge::canCancel() const
{
    return m_task && m_task->canAbort();
}

QString CreateBridge::lastGroup() const
{
    return APPLICATION->settings()->get("LastUsedGroupForNewInstance").toString();
}

QString CreateBridge::autoTest() const
{
    return qEnvironmentVariable("FFPRISM_CREATE");
}

void CreateBridge::init()
{
    if (m_versions->uid().isEmpty()) {
        m_versions->setSource("net.minecraft");
    }
}

void CreateBridge::setLoader(const QString& loader, const QString& mcVersion)
{
    const auto* info = loaderInfo(loader);
    m_loader = info ? loader : QString();
    m_loaderNote.clear();
    if (!info) {
        m_loaderVersions->setSource({});
    } else if (mcVersion.isEmpty()) {
        m_loaderVersions->setSource(info->uid, {}, true);
        m_loaderNote = tr("Pick a Minecraft version first.");
    } else if (info->fabricLike) {
        // Same rule as Prism: Fabric and Quilt exist for 1.14 and newer, any loader build fits
        const bool supported = Version(mcVersion) >= Version("1.14");
        m_loaderVersions->setSource(info->uid, {}, !supported);
    } else {
        m_loaderVersions->setSource(info->uid, mcVersion);
    }
    if (info && m_loaderNote.isEmpty()) {
        m_loaderNote = tr("%1 is not available for Minecraft %2.").arg(info->name, mcVersion);
    }
    emit loaderChanged();
}

void CreateBridge::clearPack()
{
    if (m_packJob) {
        m_packJob->abort();
    }
    m_packJob.reset();
    m_pack.reset();
    m_packInfo.clear();
    m_packVersions.clear();
    m_packError.clear();
    m_packLogoPath.clear();
    emit packChanged();
}

void CreateBridge::selectPack(int row)
{
    auto pack = m_packs->packAt(row);
    clearPack();
    if (!pack) {
        return;
    }
    m_pack = pack;
    m_packProvider = m_packs->provider();
    m_packInfo = {
        { "packId", pack->addonId.toString() },
        { "title", pack->name },
        { "summary", pack->description },
        { "iconUrl", pack->logoUrl },
        { "author", pack->authors.isEmpty() ? QString() : pack->authors.first().name },
        { "downloads", static_cast<double>(m_packs->downloadsAt(row)) },
        { "website", pack->websiteUrl },
        { "provider", m_packProvider },
    };
    fetchPackLogo();

    const bool curse = m_packProvider == "curseforge";
    const ResourceAPI& api = curse ? static_cast<const ResourceAPI&>(FlameAPI::get()) : static_cast<const ResourceAPI&>(ModrinthAPI::get());
    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks{};
    const QVariant id = pack->addonId;
    callbacks.onSucceed = [this, id](QVector<ModPlatform::IndexedVersion>& list) {
        if (!m_pack || m_pack->addonId != id) {
            return;
        }
        m_pack->versions = list;
        m_pack->versionsLoaded = true;
        m_packVersions.clear();
        for (int i = 0; i < list.size(); i++) {
            const auto& v = list.at(i);
            QStringList mc = v.mcVersion;
            mc.removeDuplicates();
            if (v.downloadUrl.isEmpty()) {
                continue;  // CurseForge files whose authors blocked third-party downloads
            }
            m_packVersions.append(QVariantMap{
                { "index", i },
                { "name", v.version.isEmpty() ? v.versionNumber : v.version },
                { "minecraft", mc.join(", ") },
                { "type", v.versionType.isValid() ? v.versionType.toString() : QString() },
                { "date", shortDate(v.date) },
            });
        }
        if (m_packVersions.isEmpty()) {
            m_packError = list.isEmpty() ? tr("This modpack has no versions.")
                                         : tr("The author doesn't allow this modpack to be downloaded by launchers.");
        }
    };
    callbacks.onFail = [this, id](const QString& reason, int) {
        if (m_pack && m_pack->addonId == id) {
            m_packError = tr("Couldn't load versions: %1").arg(reason);
        }
    };
    auto job = api.getProjectVersions({ .pack = pack, .mcVersions = {}, .loaders = {}, .resourceType = ModPlatform::ResourceType::Modpack },
                                      callbacks);
    if (!job) {
        return;
    }
    m_packJob = job;
    connect(job.get(), &Task::finished, this, [this, t = job.get()] {
        if (m_packJob.get() == t) {
            m_packJob.reset();
            emit packChanged();
        }
    });
    emit packChanged();
    job->start();
}

void CreateBridge::fetchPackLogo()
{
    if (!m_pack || m_pack->logoUrl.isEmpty() || m_pack->logoName.isEmpty()) {
        return;
    }
    const QString base = m_packProvider == "curseforge" ? "FlamePacks" : "ModrinthModpacks";
    auto entry = APPLICATION->metacache()->resolveEntry(base, QString("logos/%1").arg(m_pack->logoName));
    const QString path = entry->getFullPath();
    auto* job = new NetJob(QString("Modpack logo %1").arg(m_pack->logoName), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(Net::ApiRequest::makeCached(QUrl(m_pack->logoUrl), entry));
    const QVariant id = m_pack->addonId;
    connect(job, &NetJob::finished, this, [this, job, id, path] {
        job->deleteLater();
        if (m_pack && m_pack->addonId == id && QFileInfo::exists(path)) {
            m_packLogoPath = path;
        }
    });
    job->start();
}

QStringList CreateBridge::groups() const
{
    auto groups = APPLICATION->instances()->getGroups();
    groups.removeAll(QString());
    groups.sort(Qt::CaseInsensitive);
    return groups;
}

QVariantList CreateBridge::icons() const
{
    QVariantList out;
    auto* list = APPLICATION->icons();
    for (int i = 0; i < list->rowCount(); i++) {
        const auto idx = list->index(i);
        out.append(QVariantMap{ { "key", idx.data(Qt::UserRole) }, { "name", idx.data(Qt::DisplayRole) } });
    }
    return out;
}

QString CreateBridge::addIconFile(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    QFileInfo fi(path);
    if (!fi.exists()) {
        return {};
    }
    APPLICATION->icons()->installIcon(path, fi.fileName());
    return fi.completeBaseName();
}

QString CreateBridge::importName(const QString& source) const
{
    const QString input = source.trimmed();
    if (input.isEmpty()) {
        return {};
    }
    const QUrl url = QUrl::fromUserInput(input);
    if (url.isLocalFile()) {
        QFileInfo fi(url.toLocalFile());
        const bool isZip = QMimeDatabase().mimeTypeForFile(fi).suffixes().contains("zip") || fi.suffix() == "zip";
        if (fi.exists() && fi.isFile() && (isZip || fi.suffix() == "mrpack")) {
            return fi.completeBaseName();
        }
        return {};
    }
    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }
    const QString base = QFileInfo(url.fileName()).completeBaseName();
    return base.isEmpty() ? url.host() : base;
}

QString CreateBridge::formatCount(double n) const
{
    if (n >= 1e6) {
        return QLocale().toString(n / 1e6, 'f', n >= 1e7 ? 0 : 1) + "M";
    }
    if (n >= 1e3) {
        return QLocale().toString(n / 1e3, 'f', n >= 1e4 ? 0 : 1) + "K";
    }
    return QLocale().toString(static_cast<qint64>(n));
}

bool CreateBridge::createVanilla(const QString& mcVersion, const QString& loaderVersion, const QString& name, const QString& icon,
                                 const QString& group)
{
    if (!m_task) {
        m_packSource.clear();
    }
    auto mc = m_versions->find(mcVersion);
    if (!mc) {
        setError(tr("Pick a Minecraft version."));
        return false;
    }
    InstanceTask* task = nullptr;
    const auto* info = loaderInfo(m_loader);
    if (info) {
        auto lv = m_loaderVersions->find(loaderVersion);
        if (!lv) {
            setError(tr("Pick a %1 version, or choose “None” as the mod loader.").arg(info->name));
            return false;
        }
        task = new VanillaCreationTask(mc, info->uid, lv);
    } else {
        task = new VanillaCreationTask(mc);
    }
    const QString suggested = info ? QString("%1 %2").arg(mcVersion, info->name) : mcVersion;
    return run(task, name, suggested, QString(), icon.isEmpty() ? QString("default") : icon, group);
}

bool CreateBridge::createModpack(int versionIndex, const QString& name, const QString& icon, const QString& group)
{
    if (!m_pack || versionIndex < 0 || versionIndex >= m_pack->versions.size()) {
        setError(tr("Pick a modpack version."));
        return false;
    }
    const auto& ver = m_pack->versions.at(versionIndex);
    QMap<QString, QString> extra;
    extra.insert("pack_id", m_pack->addonId.toString());
    extra.insert("pack_version_id", ver.fileId.toString());

    QString iconKey = icon;
    if (iconKey.isEmpty()) {
        iconKey = "default";
        if (!m_packLogoPath.isEmpty() && QFileInfo::exists(m_packLogoPath)) {
            const QString logoName = (m_packProvider == "curseforge" ? "curseforge_" : "modrinth_") + m_pack->logoName;
            APPLICATION->icons()->installIcon(m_packLogoPath, logoName);
            iconKey = logoName.left(logoName.lastIndexOf('.') > 0 ? logoName.lastIndexOf('.') : logoName.size());
        }
    }
    auto* task = new InstanceImportTask(QUrl(ver.downloadUrl), true, nullptr, std::move(extra));
    m_packSource = QUrl(ver.downloadUrl);
    return run(task, name, m_pack->name, m_packProvider == "modrinth" ? ver.version : QString(), iconKey, group);
}

bool CreateBridge::createImport(const QString& source, const QString& name, const QString& icon, const QString& group)
{
    QString input = source.trimmed();
    QUrl url = QUrl::fromUserInput(input);
    if (url.scheme() == "curseforge") {
        // curseforge://install?addonId=..&fileId=.. — resolve the real download first, like Prism's import page
        QUrlQuery query(url);
        const QString addonId = query.queryItemValue("addonId");
        const QString fileId = query.queryItemValue("fileId");
        if (addonId.isEmpty() || fileId.isEmpty()) {
            setError(tr("This CurseForge link is incomplete."));
            return false;
        }
        auto [job, array] = FlameAPI::getFile(addonId, fileId);
        m_task = job;
        m_status = tr("Looking up the modpack…");
        m_progress = -1;
        emit busyChanged();
        emit progressChanged();
        connect(job.get(), &Task::finished, this, [this, t = job.get(), array, addonId, fileId, name, icon, group] {
            if (m_task.get() != t) {
                return;
            }
            const bool ok = t->wasSuccessful();
            const QString reason = t->failReason();
            const QByteArray response = *array;
            m_task.reset();
            emit busyChanged();
            if (!ok) {
                setError(reason);
                return;
            }
            auto doc = Json::requireDocument(response);
            if (!doc) {
                setError(doc.error());
                return;
            }
            auto data = doc->object()["data"].toObject();
            const QString fileName = data["fileName"].toString();
            const QUrl dl(data["downloadUrl"].toString());
            if (!fileName.endsWith(".zip")) {
                setError(tr("This link isn't a modpack."));
                return;
            }
            if (!dl.isValid() || dl.isEmpty()) {
                setError(tr("The author of %1 doesn't allow launchers to download it. Download it in the browser and import the file.")
                             .arg(fileName));
                return;
            }
            QMap<QString, QString> extra;
            extra.insert("pack_id", addonId);
            extra.insert("pack_version_id", fileId);
            const QString packName = data["displayName"].toString(QFileInfo(dl.fileName()).completeBaseName());
            m_packSource = dl;
            run(new InstanceImportTask(dl, false, nullptr, std::move(extra)), name.isEmpty() ? packName : name, packName, QString(),
                icon.isEmpty() ? QString("default") : icon, group);
        });
        job->start();
        return true;
    }
    const QString suggested = importName(input);
    if (suggested.isEmpty()) {
        setError(tr("Choose a .zip or .mrpack file, or paste a link to one."));
        return false;
    }
    if (input.endsWith("?client=y")) {
        input.chop(9);
        input.append("/file");
        url = QUrl::fromUserInput(input);
    }
    m_packSource = url;
    return run(new InstanceImportTask(url, false, nullptr), name, suggested, QString(), icon.isEmpty() ? QString("default") : icon, group);
}

bool CreateBridge::run(InstanceTask* raw, const QString& name, const QString& originalName, const QString& version, const QString& icon,
                       const QString& group)
{
    if (m_task) {
        delete raw;
        return false;
    }
    clearError();
    const QString finalName = name.trimmed().isEmpty() ? originalName.trimmed() : name.trimmed();
    raw->setName(finalName);
    raw->setOriginalName(originalName.trimmed(), version);
    raw->setGroup(group.trimmed());
    raw->setIcon(icon);
    const QString templateDir = APPLICATION->settings()->get("TemplateDir").toString();
    raw->setCopyTemplateDir(!templateDir.isEmpty() && QDir(templateDir).exists());
    APPLICATION->settings()->set("LastUsedGroupForNewInstance", group.trimmed());

    m_createdName = finalName;
    return start(Task::Ptr(APPLICATION->instances()->wrapInstanceTask(raw)));
}

bool CreateBridge::start(const Task::Ptr& task)
{
    m_createdId.clear();
    m_status = tr("Preparing…");
    m_progress = -1;
    m_task = task;

    auto selectConn = std::make_shared<QMetaObject::Connection>();
    *selectConn = connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this,
                          [this](const QString& id) { m_createdId = id; });
    connect(task.get(), &Task::status, this, [this](const QString& s) {
        m_status = s;
        emit progressChanged();
    });
    connect(task.get(), &Task::progress, this, [this](qint64 current, qint64 total) {
        m_progress = total > 0 ? qBound(0.0, static_cast<double>(current) / static_cast<double>(total), 1.0) : -1;
        emit progressChanged();
    });
    connect(task.get(), &Task::abortStatusChanged, this, [this] { emit progressChanged(); });
    connect(task.get(), &Task::finished, this, [this, t = task.get(), selectConn] {
        disconnect(*selectConn);
        if (m_task.get() != t) {
            return;
        }
        const bool ok = t->wasSuccessful();
        const bool aborted = t->getState() == Task::State::AbortedByUser;
        const QString reason = t->failReason();
        const QUrl packSource = std::exchange(m_packSource, QUrl());
        m_task.reset();
        m_status.clear();
        emit busyChanged();
        emit progressChanged();
        if (ok) {
            qInfo() << "[create] created instance" << m_createdId << m_createdName;
            applyPackSettings(packSource);
            emit created(m_createdId, m_createdName);
        } else if (!aborted) {
            qWarning() << "[create] failed:" << reason;
            setError(reason.isEmpty() ? tr("Creating the instance failed.") : reason);
        }
    });
    emit busyChanged();
    emit progressChanged();
    task->start();
    return true;
}

void CreateBridge::cancel()
{
    if (m_task && m_task->canAbort()) {
        m_task->abort();
    }
}

void CreateBridge::setError(const QString& e)
{
    m_error = e;
    emit errorChanged();
}

void CreateBridge::clearError()
{
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
}

void CreateBridge::finishAutoTest(bool ok)
{
    qInfo() << "[create] auto test finished, ok =" << ok;
    if (qEnvironmentVariableIntValue("FFPRISM_CREATE_QUIT")) {
        QCoreApplication::exit(ok ? 0 : 1);
    }
}

void CreateBridge::applyPackSettings(const QUrl& source)
{
    // fashion-launcher.json inside the modpack (docs/MODPACK_SETTINGS.md): the archive is either the picked
    // file or the copy InstanceImportTask downloaded into the cache
    if (source.isEmpty() || m_createdId.isEmpty()) {
        return;
    }
    QString archive;
    if (source.isLocalFile()) {
        archive = source.toLocalFile();
    } else {
        archive = APPLICATION->metacache()->resolveEntry("general", source.host() + '/' + source.path())->getFullPath();
    }
    auto* inst = APPLICATION->instances()->getInstanceById(m_createdId);
    const QString said = ToolsBridge::applyPackSettings(inst, archive);
    if (!said.isEmpty()) {
        ToolsBridge::announce(tr("“%1” is ready. %2").arg(m_createdName, said));
    }
}

bool CreateBridge::createFromLaunchers(const QVariantList& keys, bool link, const QString& name, const QString& icon, const QString& group)
{
    if (m_task) {
        return false;
    }
    m_packSource.clear();
    auto* tools = ToolsBridge::instance();
    QList<QVariantMap> entries;
    for (const auto& key : keys) {
        const auto e = tools ? tools->launcherEntry(key.toString()) : QVariantMap();
        if (e.value("importable").toBool()) {
            entries << e;
        }
    }
    if (entries.isEmpty()) {
        setError(tr("Pick at least one instance to import."));
        return false;
    }
    const QString iconKey = icon.isEmpty() ? QString("default") : icon;
    if (entries.size() == 1) {
        const auto& e = entries.first();
        return run(LauncherImport::makeTask(e, link), name, e.value("name").toString(), e.value("mc").toString(), iconKey, group);
    }
    // Several at once: one after another, each keeps its own name
    clearError();
    const QString templateDir = APPLICATION->settings()->get("TemplateDir").toString();
    auto sequence = makeShared<SequentialTask>(tr("Importing instances"));
    for (const auto& e : entries) {
        auto* raw = LauncherImport::makeTask(e, link);
        raw->setName(e.value("name").toString());
        raw->setOriginalName(e.value("name").toString(), e.value("mc").toString());
        raw->setGroup(group.trimmed());
        raw->setIcon(iconKey);
        raw->setCopyTemplateDir(!templateDir.isEmpty() && QDir(templateDir).exists());
        sequence->addTask(Task::Ptr(APPLICATION->instances()->wrapInstanceTask(raw)));
    }
    APPLICATION->settings()->set("LastUsedGroupForNewInstance", group.trimmed());
    m_createdName = tr("%n instance(s)", nullptr, static_cast<int>(entries.size()));
    return start(sequence);
}
