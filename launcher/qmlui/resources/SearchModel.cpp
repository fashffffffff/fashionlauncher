// SPDX-License-Identifier: GPL-3.0-only
#include "SearchModel.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "ResourceCommon.h"
#include "ResourceDownloadTask.h"
#include "qmlui/jobs/TaskCenter.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "net/RPCSink.h"
#include "settings/SettingsObject.h"
#include "tasks/ConcurrentTask.h"

static constexpr int PageSize = 25;

SearchModel::SearchModel(QObject* parent) : QAbstractListModel(parent)
{
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(300);
    connect(&m_searchTimer, &QTimer::timeout, this, &SearchModel::search);
}

SearchModel::~SearchModel()
{
    ++m_generation;
    if (m_searchJob) {
        m_searchJob->disconnect(this);
        m_searchJob->abort();
    }
    if (m_categoryJob) {
        m_categoryJob->disconnect(this);
    }
    for (auto& t : m_versionJobs) {
        t->disconnect(this);
    }
    // Installs keep running in the background; just stop reporting to this (destroyed) model.
    for (auto& t : m_installTasks) {
        t->disconnect(this);
    }
    if (m_folder) {
        m_folder->disconnect(this);
    }
}

const ResourceAPI& SearchModel::api() const
{
    if (isFlame()) {
        return FlameAPI::get();
    }
    return ModrinthAPI::get();
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {
        { ProjectIdRole, "projectId" }, { TitleRole, "title" }, { AuthorRole, "author" },
        { DownloadsRole, "downloads" }, { SummaryRole, "summary" }, { IconUrlRole, "iconUrl" },
        { UrlRole, "url" },             { StateRole, "state" },  { ProgressRole, "progress" },
    };
}

int SearchModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_packs.size());
}

QVariant SearchModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_packs.size()) {
        return {};
    }
    const auto& pack = m_packs.at(index.row());
    const QString id = pack->addonId.toString();
    switch (role) {
        case ProjectIdRole:
            return id;
        case TitleRole:
            return pack->name;
        case AuthorRole: {
            QStringList names;
            for (const auto& a : pack->authors) {
                names << a.name;
            }
            return names.join(", ");
        }
        case DownloadsRole:
            return static_cast<double>(m_downloads.value(id, -1));
        case SummaryRole:
            return pack->description;
        case IconUrlRole:
            return pack->logoUrl;
        case UrlRole:
            return pack->websiteUrl;
        case StateRole:
            return stateOf(id);
        case ProgressRole:
            return m_installing.value(id, -1);
        default:
            return {};
    }
}

// ---- properties ------------------------------------------------------------------------------

void SearchModel::setInstanceId(const QString& id)
{
    if (id == m_instanceId) {
        return;
    }
    m_instanceId = id;
    m_instance = QmlResources::findInstance(id);
    attachFolder();
    emit instanceChanged();
    scheduleSearch();
}

void SearchModel::setKind(const QString& k)
{
    if (k == m_kind || k.isEmpty()) {
        return;
    }
    m_kind = k;
    m_category.clear();
    attachFolder();
    emit kindChanged();
    emit categoryChanged();
    loadCategories();
    scheduleSearch();
}

void SearchModel::setPlatform(const QString& p)
{
    if (p == m_platform) {
        return;
    }
    m_platform = p;
    m_sortIndex = 0;
    m_category.clear();
    emit platformChanged();
    emit sortIndexChanged();
    emit categoryChanged();
    loadCategories();
    scheduleSearch();
}

void SearchModel::setQuery(const QString& q)
{
    if (q == m_query) {
        return;
    }
    m_query = q;
    emit queryChanged();
    scheduleSearch();
}

void SearchModel::setSortIndex(int i)
{
    if (i == m_sortIndex) {
        return;
    }
    m_sortIndex = i;
    emit sortIndexChanged();
    scheduleSearch();
}

void SearchModel::setCategory(const QString& c)
{
    if (c == m_category) {
        return;
    }
    m_category = c;
    emit categoryChanged();
    scheduleSearch();
}

QVariantList SearchModel::sortOptions() const
{
    QVariantList out;
    for (const auto& m : api().getSortingMethods()) {
        out << QVariantMap{ { "label", m.readableName } };
    }
    return out;
}

QString SearchModel::gameVersion() const
{
    return QmlResources::gameVersion(m_instance);
}

QString SearchModel::loaderText() const
{
    return QmlResources::loaderNames(m_instance);
}

bool SearchModel::hasModLoader() const
{
    return QmlResources::hasModLoader(m_instance);
}

// ---- searching -------------------------------------------------------------------------------

void SearchModel::scheduleSearch()
{
    if (m_categories.isEmpty() && !m_categoryJob) {
        loadCategories();
    }
    m_searchTimer.start();
}

void SearchModel::search()
{
    m_searchTimer.stop();
    runSearch(false);
}

void SearchModel::loadMore()
{
    if (m_canLoadMore && !m_busy) {
        runSearch(true);
    }
}

void SearchModel::runSearch(bool append)
{
    ++m_generation;
    if (m_searchJob) {
        m_searchJob->disconnect(this);
        m_searchJob->abort();
        m_searchJob.reset();
    }
    if (!append) {
        beginResetModel();
        m_packs.clear();
        m_downloads.clear();
        endResetModel();
        m_offset = 0;
        setSelectedRow(-1);
        emit countChanged();
    }
    if (!m_instance) {
        m_busy = false;
        m_canLoadMore = false;
        m_error.clear();
        emit busyChanged();
        return;
    }

    const auto type = QmlResources::typeFor(m_kind);
    ResourceAPI::SearchArgs args;
    args.type = type;
    args.offset = m_offset;
    if (!m_query.trimmed().isEmpty()) {
        args.search = m_query.trimmed();
    }
    const auto methods = api().getSortingMethods();
    if (m_sortIndex >= 0 && m_sortIndex < methods.size()) {
        args.sorting = methods.at(m_sortIndex);
    }
    if (type == ModPlatform::ResourceType::Mod) {
        // Only show mods that run on this instance.
        args.loaders = m_instance->getPackProfile()->getSupportedModLoaders();
        const QString mc = gameVersion();
        if (!mc.isEmpty()) {
            args.versions = std::vector<Version>{ Version(mc) };
        }
    }
    if (!m_category.isEmpty()) {
        args.categoryIds = QStringList{ m_category };
    }

    auto spec = api().searchProjects(args);
    // The pack structs carry no download counts; read them from the same response.
    auto downloads = std::make_shared<QHash<QString, qint64>>();
    const bool flame = isFlame();
    auto parse = spec.second;
    spec.second = [parse, downloads, flame](const QByteArray& data) {
        const auto doc = QJsonDocument::fromJson(data).object();
        for (const auto& v : doc.value(flame ? "data" : "hits").toArray()) {
            const auto o = v.toObject();
            const QString id = flame ? QString::number(o.value("id").toInteger()) : o.value("project_id").toString();
            downloads->insert(id, o.value(flame ? "downloadCount" : "downloads").toInteger());
        }
        return parse(data);
    };
    auto job = makeShared<NetJob>(QString("QmlSearch"), APPLICATION->network());
    auto [action, response] = Net::RPC::make<QList<ModPlatform::IndexedPack>>(spec);
    if (!action) {
        return;
    }
    job->addNetAction(action);
    m_searchJob = job;
    m_busy = true;
    m_error.clear();
    emit busyChanged();

    const int gen = m_generation;
    connect(job.get(), &NetJob::succeeded, this, [this, gen, response, downloads, type] {
        if (gen != m_generation) {
            return;
        }
        QList<ModPlatform::IndexedPack::Ptr> fresh;
        for (const auto& p : *response) {
            auto ptr = std::make_shared<ModPlatform::IndexedPack>(p);
            if (ptr->resourceType == ModPlatform::ResourceType::Unknown) {
                ptr->resourceType = type;
            }
            fresh << ptr;
        }
        m_canLoadMore = fresh.size() >= PageSize;
        m_offset += PageSize;
        if (!fresh.isEmpty()) {
            beginInsertRows({}, static_cast<int>(m_packs.size()), static_cast<int>(m_packs.size() + fresh.size() - 1));
            m_packs.append(fresh);
            m_downloads.insert(*downloads);
            endInsertRows();
            emit countChanged();
        }
    });
    connect(job.get(), &NetJob::failed, this, [this, gen](const QString& reason) {
        if (gen == m_generation) {
            m_error = reason.isEmpty() ? tr("Could not reach the server") : reason;
            m_canLoadMore = false;
        }
    });
    connect(job.get(), &NetJob::finished, this, [this, gen] {
        if (gen != m_generation) {
            return;
        }
        m_busy = false;
        m_searchJob.reset();
        emit busyChanged();
    });
    job->start();
}

void SearchModel::loadCategories()
{
    if (m_categoryJob) {
        m_categoryJob->disconnect(this);
        m_categoryJob.reset();
    }
    m_categories.clear();
    emit categoriesChanged();

    const auto type = QmlResources::typeFor(m_kind);
    const bool flame = isFlame();
    std::pair<Task::Ptr, QByteArray*> job = flame ? FlameAPI::getCategories(type) : ModrinthAPI::get().getModCategories();
    if (!job.first) {
        return;
    }
    m_categoryJob = job.first;
    QByteArray* response = job.second;
    const QString mrType = type == ModPlatform::ResourceType::ResourcePack ? "resourcepack"
                         : type == ModPlatform::ResourceType::ShaderPack   ? "shader"
                                                                           : "mod";
    auto task = job.first;
    connect(task.get(), &Task::succeeded, this, [this, response, flame, mrType] {
        const auto list = flame ? FlameAPI::get().loadModCategories(*response) : ModrinthAPI::loadCategories(*response, mrType);
        QVariantList out;
        for (const auto& c : list) {
            QString name = c.name;
            name.replace('-', ' ');
            if (!name.isEmpty()) {
                name[0] = name[0].toUpper();
            }
            out << QVariantMap{ { "id", c.id }, { "name", name } };
        }
        std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap().value("name").toString().localeAwareCompare(b.toMap().value("name").toString()) < 0;
        });
        m_categories = out;
        emit categoriesChanged();
    });
    connect(task.get(), &Task::finished, this, [this, task] {
        if (m_categoryJob == task) {
            m_categoryJob.reset();
        }
    });
    task->start();
}

// ---- details & versions ----------------------------------------------------------------------

void SearchModel::setSelectedRow(int row)
{
    if (row >= m_packs.size()) {
        row = -1;
    }
    if (row == m_selectedRow) {
        return;
    }
    m_selectedRow = row;
    if (row >= 0) {
        loadVersions(row);
    }
    emit selectedChanged();
}

QVariantMap SearchModel::selected() const
{
    if (m_selectedRow < 0 || m_selectedRow >= m_packs.size()) {
        return {};
    }
    const auto& pack = m_packs.at(m_selectedRow);
    const QModelIndex idx = index(m_selectedRow);
    const QString id = pack->addonId.toString();
    QVariantList versions;
    if (pack->versionsLoaded) {
        for (const auto& v : pack->versions) {
            QStringList loaders;
            for (auto l : ModPlatform::modLoaderTypesToList(v.loaders)) {
                loaders << ModPlatform::getModLoaderAsString(l);
            }
            QStringList mc = v.mcVersion;
            mc.removeDuplicates();
            versions << QVariantMap{
                { "name", v.version.isEmpty() ? v.versionNumber : v.version },
                { "number", v.versionNumber },
                { "type", v.versionType.isValid() ? v.versionType.toString() : QString() },
                { "date", v.date.left(10) },
                { "mc", mc.join(", ") },
                { "loaders", loaders.join(", ") },
                { "fileName", v.fileName },
            };
        }
    }
    return {
        { "row", m_selectedRow },
        { "projectId", id },
        { "title", pack->name },
        { "author", data(idx, AuthorRole) },
        { "downloads", data(idx, DownloadsRole) },
        { "summary", pack->description },
        { "iconUrl", pack->logoUrl },
        { "url", pack->websiteUrl },
        { "state", stateOf(id) },
        { "progress", m_installing.value(id, -1) },
        { "versionsLoaded", pack->versionsLoaded },
        { "versionsLoading", m_versionJobs.contains(id) },
        { "versionsError", m_versionErrors.contains(id) },
        { "versions", versions },
    };
}

void SearchModel::loadVersions(int row, std::function<void()> then)
{
    if (row < 0 || row >= m_packs.size() || !m_instance) {
        return;
    }
    auto pack = m_packs.at(row);
    const QString id = pack->addonId.toString();
    if (pack->versionsLoaded) {
        if (then) {
            then();
        }
        return;
    }
    if (m_versionJobs.contains(id)) {
        return;
    }
    m_versionErrors.remove(id);

    ResourceAPI::VersionSearchArgs args;
    args.pack = pack;
    args.resourceType = QmlResources::typeFor(m_kind);
    if (args.resourceType == ModPlatform::ResourceType::Mod) {
        const QString mc = gameVersion();
        if (!mc.isEmpty()) {
            args.mcVersions = std::vector<Version>{ Version(mc) };
        }
        args.loaders = m_instance->getPackProfile()->getSupportedModLoaders();
    }

    QPointer<SearchModel> self(this);
    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> cb;
    cb.onSucceed = [self, pack, then](QVector<ModPlatform::IndexedVersion>& list) {
        pack->versions = QList<ModPlatform::IndexedVersion>(list.begin(), list.end());
        pack->versionsLoaded = true;
        if (self && then) {
            then();
        }
    };
    cb.onFail = [self, id](const QString&, int) {
        if (self) {
            self->m_versionErrors.insert(id);
        }
    };
    auto task = api().getProjectVersions(args, cb);
    if (!task) {
        return;
    }
    m_versionJobs.insert(id, task);
    connect(task.get(), &Task::finished, this, [this, id] {
        m_versionJobs.remove(id);
        notifyProject(id);
    });
    notifyProject(id);
    task->start();
}

// ---- installing ------------------------------------------------------------------------------

void SearchModel::install(int row, int versionIndex)
{
    if (row < 0 || row >= m_packs.size() || !m_instance || !m_folder) {
        return;
    }
    auto pack = m_packs.at(row);
    const QString id = pack->addonId.toString();
    if (m_installTasks.contains(id)) {
        return;
    }
    if (!pack->versionsLoaded) {
        // Fetch versions first, then take the newest compatible one.
        m_installing.insert(id, -1);
        notifyProject(id);
        loadVersions(row, [this, pack, id] {
            m_installing.remove(id);
            if (pack->versions.isEmpty()) {
                notifyProject(id);
                emit message(tr("“%1” has no version for this instance").arg(pack->name), true);
                return;
            }
            startInstall(pack, pack->versions.first());
        });
        return;
    }
    if (pack->versions.isEmpty()) {
        emit message(tr("“%1” has no version for this instance").arg(pack->name), true);
        return;
    }
    if (versionIndex < 0 || versionIndex >= pack->versions.size()) {
        versionIndex = 0;
    }
    startInstall(pack, pack->versions.at(versionIndex));
}

void SearchModel::startInstall(const ModPlatform::IndexedPack::Ptr& pack, const ModPlatform::IndexedVersion& version)
{
    const QString id = pack->addonId.toString();
    if (!m_folder || !m_instance) {
        return;
    }
    const bool indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
    QPointer<ResourceFolderModel> folder = m_folder;
    QPointer<MinecraftInstance> inst = m_instance;
    m_installing.insert(id, -1);
    notifyProject(id);

    auto download = [this, id, pack, version, indexed, folder, inst](const QList<std::shared_ptr<GetModDependenciesTask::PackDependency>>& deps,
                                                               const QHash<QString, GetModDependenciesTask::PackDependencyExtraInfo>& extra) {
        if (!folder) {
            m_installing.remove(id);
            m_installTasks.remove(id);
            notifyProject(id);
            return;
        }
        auto tasks = makeShared<ConcurrentTask>(tr("Installing %1").arg(pack->name),
                                                APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
        tasks->addTask(makeShared<ResourceDownloadTask>(pack, version, folder, indexed));
        QStringList depNames;
        for (const auto& dep : deps) {
            const auto info = extra.value(dep->pack->addonId.toString());
            if (info.maybeInstalled) {
                continue;
            }
            tasks->addTask(makeShared<ResourceDownloadTask>(dep->pack, dep->version, folder, indexed, "dependency", info.requiredByIds.value(0)));
            depNames << dep->pack->name;
        }
        m_installTasks.insert(id, tasks);
        connect(tasks.get(), &Task::progress, this, [this, id](qint64 cur, qint64 total) {
            m_installing.insert(id, total > 0 ? double(cur) / double(total) : -1);
            notifyProject(id);
        });
        auto weak = tasks.toWeakRef();
        connect(tasks.get(), &Task::finished, this, [this, id, weak, pack, depNames, folder] {
            auto t = weak.lock();
            const bool ok = t && t->wasSuccessful();
            m_installing.remove(id);
            m_installTasks.remove(id);
            if (ok) {
                m_installedIds.insert(id);
                emit message(depNames.isEmpty() ? tr("Installed “%1”").arg(pack->name)
                                                : tr("Installed “%1” with %2").arg(pack->name, depNames.join(", ")),
                             false);
            } else {
                emit message(tr("Could not install “%1”: %2").arg(pack->name, t ? t->failReason() : QString()), true);
            }
            if (folder) {
                folder->update();
            }
            notifyProject(id);
        });
        TaskCenter::instance()->track(tasks, tr("Installing %1").arg(pack->name), "resources", inst ? inst->id() : QString(),
                                      pack->logoUrl);
        tasks->start();
    };

    auto* mods = dynamic_cast<ModFolderModel*>(m_folder.data());
    if (!mods || APPLICATION->settings()->get("ModDependenciesDisabled").toBool()) {
        download({}, {});
        return;
    }
    // Look up required dependencies first (the same task the classic download dialog uses).
    auto depTask = makeShared<GetModDependenciesTask>(m_instance, mods,
                                                      QList{ std::make_shared<GetModDependenciesTask::PackDependency>(pack, version) });
    m_installTasks.insert(id, depTask);
    auto weak = depTask.toWeakRef();
    connect(depTask.get(), &Task::finished, this, [this, weak, download, id] {
        auto t = weak.lock();
        m_installTasks.remove(id);
        if (!t) {
            return;
        }
        // A failed lookup shouldn't block installing the mod itself.
        download(t->wasSuccessful() ? t->getDependecies() : QList<std::shared_ptr<GetModDependenciesTask::PackDependency>>{},
                 t->wasSuccessful() ? t->getExtraInfo() : QHash<QString, GetModDependenciesTask::PackDependencyExtraInfo>{});
    });
    depTask->start();
}

void SearchModel::openWebsite(int row) const
{
    if (row >= 0 && row < m_packs.size() && !m_packs.at(row)->websiteUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_packs.at(row)->websiteUrl));
    }
}

// ---- installed state -------------------------------------------------------------------------

void SearchModel::attachFolder()
{
    if (m_folder) {
        m_folder->disconnect(this);
    }
    m_folder = QmlResources::folderFor(m_instance, m_kind);
    m_installedIds.clear();
    if (m_folder) {
        connect(m_folder, &ResourceFolderModel::updateFinished, this, &SearchModel::refreshInstalled);
        if (m_folder->empty()) {
            m_folder->update();
        } else {
            refreshInstalled();
        }
    }
    if (!m_packs.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_packs.size()) - 1), { StateRole });
    }
}

void SearchModel::refreshInstalled()
{
    if (!m_folder) {
        return;
    }
    QSet<QString> ids;
    for (auto* res : m_folder->allResources()) {
        if (auto meta = res->metadata(); meta && meta->projectId.isValid()) {
            ids.insert(meta->projectId.toString());
        }
    }
    // Keep fresh installs marked until their metadata shows up.
    for (const auto& id : std::as_const(m_installedIds)) {
        if (m_installing.contains(id)) {
            ids.insert(id);
        }
    }
    m_installedIds = ids;
    if (!m_packs.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_packs.size()) - 1), { StateRole });
    }
    emit selectedChanged();
}

QString SearchModel::stateOf(const QString& projectId) const
{
    if (m_installing.contains(projectId)) {
        return "installing";
    }
    return m_installedIds.contains(projectId) ? "installed" : QString();
}

int SearchModel::rowOf(const QString& projectId) const
{
    for (int i = 0; i < m_packs.size(); ++i) {
        if (m_packs.at(i)->addonId.toString() == projectId) {
            return i;
        }
    }
    return -1;
}

void SearchModel::notifyProject(const QString& projectId)
{
    const int row = rowOf(projectId);
    if (row >= 0) {
        emit dataChanged(index(row), index(row), { StateRole, ProgressRole });
        if (row == m_selectedRow) {
            emit selectedChanged();
        }
    }
}
