// SPDX-License-Identifier: GPL-3.0-only
#include "InstancesModel.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/Mode.h"

InstancesModel::InstancesModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    setSourceModel(APPLICATION->instances());
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    sort(0, Qt::AscendingOrder);

    auto emitCount = [this] { emit countChanged(); };
    connect(this, &QAbstractItemModel::rowsInserted, this, emitCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, emitCount);
    connect(this, &QAbstractItemModel::modelReset, this, emitCount);
    connect(APPLICATION->instances(), &InstanceList::groupsChanged, this, &InstancesModel::groupsChanged);
    // Version/loader can change when the instance is edited; drop its cache entry.
    connect(APPLICATION->instances(), &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& tl, const QModelIndex& br) {
        for (int r = tl.row(); r <= br.row(); r++) {
            if (auto* inst = APPLICATION->instances()->at(r)) {
                m_meta.remove(inst->id());
            }
        }
    });
}

QHash<int, QByteArray> InstancesModel::roleNames() const
{
    return {
        { IdRole, "instanceId" },     { NameRole, "name" },         { IconRole, "icon" },         { GameVersionRole, "gameVersion" },
        { LoaderRole, "loader" },     { LastPlayedRole, "lastPlayed" }, { PlayTimeRole, "playTime" }, { RunningRole, "running" },
        { BackdropRole, "backdrop" }, { GroupRole, "group" },       { NotesRole, "notes" },       { PackNameRole, "packName" },
        { GameRootRole, "gameRoot" }, { InstanceRootRole, "instanceRoot" },
    };
}

MinecraftInstance* InstancesModel::instanceAt(const QModelIndex& index) const
{
    auto src = mapToSource(index);
    if (!src.isValid()) {
        return nullptr;
    }
    return static_cast<MinecraftInstance*>(src.data(InstanceList::InstancePointerRole).value<void*>());
}

const InstancesModel::Meta& InstancesModel::meta(MinecraftInstance* inst) const
{
    auto it = m_meta.find(inst->id());
    if (it != m_meta.end()) {
        return *it;
    }
    Meta m;
    auto* profile = inst->getPackProfile();
    m.gameVersion = profile->getComponentVersion("net.minecraft");
    if (m.gameVersion.isEmpty()) {
        if (auto res = profile->reload(Net::Mode::Offline); !res) {
            qWarning() << "Failed to reload components:" << res.error();
        }
        m.gameVersion = profile->getComponentVersion("net.minecraft");
    }
    static const QList<QPair<QString, QString>> loaders = {
        { "net.neoforged", "NeoForge" },
        { "net.minecraftforge", "Forge" },
        { "net.fabricmc.fabric-loader", "Fabric" },
        { "org.quiltmc.quilt-loader", "Quilt" },
        { "com.mumfrey.liteloader", "LiteLoader" },
    };
    for (const auto& [uid, label] : loaders) {
        if (!profile->getComponentVersion(uid).isEmpty()) {
            m.loader = label;
            break;
        }
    }

    // Newest screenshot becomes the instance's backdrop.
    QDir shots(QDir(inst->gameRoot()).filePath("screenshots"));
    const auto files = shots.entryInfoList({ "*.png", "*.jpg", "*.jpeg" }, QDir::Files, QDir::Time);
    if (!files.isEmpty()) {
        m.backdrop = QUrl::fromLocalFile(files.first().absoluteFilePath()).toString();
    }
    return *m_meta.insert(inst->id(), m);
}

QVariant InstancesModel::data(const QModelIndex& index, int role) const
{
    auto* inst = instanceAt(index);
    if (!inst) {
        return {};
    }
    switch (role) {
        case IdRole:
            return inst->id();
        case NameRole:
            return inst->name();
        case IconRole:
            return QString("image://instanceicon/%1").arg(inst->iconKey());
        case GameVersionRole:
            return meta(inst).gameVersion;
        case LoaderRole:
            return meta(inst).loader;
        case LastPlayedRole:
            return QVariant::fromValue<qint64>(inst->lastLaunch());
        case PlayTimeRole:
            return QVariant::fromValue<qint64>(inst->totalTimePlayed());
        case RunningRole:
            return inst->isRunning();
        case BackdropRole:
            return meta(inst).backdrop;
        case GroupRole:
            return mapToSource(index).data(InstanceList::GroupRole);
        case NotesRole:
            return inst->notes();
        case PackNameRole:
            return inst->isManagedPack() ? inst->getManagedPackName() : QString();
        case GameRootRole:
            return QDir(inst->gameRoot()).absolutePath();
        case InstanceRootRole:
            return QDir(inst->instanceRoot()).absolutePath();
        default:
            return QSortFilterProxyModel::data(index, role);
    }
}

void InstancesModel::setFilter(const QString& filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}

void InstancesModel::setSortMode(const QString& mode)
{
    if (mode == m_sortMode) {
        return;
    }
    m_sortMode = mode;
    invalidate();
    emit sortModeChanged();
}

void InstancesModel::setGroup(const QString& group)
{
    if (group == m_group) {
        return;
    }
    m_group = group;
    invalidateFilter();
    emit groupChanged();
    emit countChanged();
}

QStringList InstancesModel::groups() const
{
    auto list = APPLICATION->instances()->getGroups();
    list.removeAll(QString());
    list.sort(Qt::CaseInsensitive);
    return list;
}

bool InstancesModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    auto idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!m_group.isEmpty()) {
        const QString g = idx.data(InstanceList::GroupRole).toString();
        if (m_group == "-" ? !g.isEmpty() : g != m_group) {
            return false;
        }
    }
    if (m_filter.isEmpty()) {
        return true;
    }
    return idx.data(Qt::DisplayRole).toString().contains(m_filter, Qt::CaseInsensitive) ||
           idx.data(InstanceList::GroupRole).toString().contains(m_filter, Qt::CaseInsensitive);
}

bool InstancesModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    auto* a = static_cast<MinecraftInstance*>(left.data(InstanceList::InstancePointerRole).value<void*>());
    auto* b = static_cast<MinecraftInstance*>(right.data(InstanceList::InstancePointerRole).value<void*>());
    if (!a || !b) {
        return false;
    }
    if (m_sortMode == "name") {
        return a->name().localeAwareCompare(b->name()) < 0;
    }
    if (m_sortMode == "playtime" && a->totalTimePlayed() != b->totalTimePlayed()) {
        return a->totalTimePlayed() > b->totalTimePlayed();
    }
    // Most recently played first; never-played instances fall back to name order.
    if (a->lastLaunch() != b->lastLaunch()) {
        return a->lastLaunch() > b->lastLaunch();
    }
    return a->name().localeAwareCompare(b->name()) < 0;
}

QVariantMap InstancesModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= rowCount()) {
        return map;
    }
    auto idx = index(row, 0);
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        map.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    }
    return map;
}

int InstancesModel::indexOf(const QString& id) const
{
    for (int i = 0; i < rowCount(); i++) {
        if (data(index(i, 0), IdRole).toString() == id) {
            return i;
        }
    }
    return -1;
}

void InstancesModel::refreshMeta()
{
    m_meta.clear();
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { GameVersionRole, LoaderRole, BackdropRole });
    }
}
