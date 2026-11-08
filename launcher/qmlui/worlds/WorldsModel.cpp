// SPDX-License-Identifier: GPL-3.0-only
#include "WorldsModel.h"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QUrl>

#include "Application.h"
#include "DesktopServices.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/World.h"
#include "minecraft/WorldList.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "qmlui/quickplay/QuickPlayBridge.h"
#include "tasks/Task.h"

WorldsModel::WorldsModel(QObject* parent) : QIdentityProxyModel(parent)
{
    auto emitCount = [this] { emit countChanged(); };
    connect(this, &QAbstractItemModel::rowsInserted, this, emitCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, emitCount);
    connect(this, &QAbstractItemModel::modelReset, this, emitCount);
}

void WorldsModel::setInstanceId(const QString& id)
{
    if (id == m_instanceId) {
        return;
    }
    if (m_worlds) {
        m_worlds->stopWatching();
    }
    m_instanceId = id;
    m_instance = APPLICATION->instances()->getInstanceById(id);
    m_worlds = m_instance ? m_instance->worldList() : nullptr;
    setSourceModel(m_worlds);
    if (m_worlds) {
        m_worlds->startWatching();
        m_worlds->update();
    }
    emit instanceIdChanged();
    emit countChanged();
}

QHash<int, QByteArray> WorldsModel::roleNames() const
{
    return { { NameRole, "name" }, { FolderRole, "folder" }, { ModeRole, "mode" }, { LastPlayedRole, "lastPlayed" },
             { SizeRole, "size" }, { IconRole, "icon" },     { SeedRole, "seed" } };
}

World* WorldsModel::worldAt(int row) const
{
    if (!m_worlds || row < 0 || row >= rowCount()) {
        return nullptr;
    }
    return static_cast<World*>(m_worlds->data(m_worlds->index(row), WorldList::ObjectRole).value<void*>());
}

QVariant WorldsModel::data(const QModelIndex& index, int role) const
{
    if (role < NameRole) {
        return QIdentityProxyModel::data(index, role);
    }
    const World* world = worldAt(index.row());
    if (!world) {
        return {};
    }
    switch (role) {
        case NameRole:
            return world->name();
        case FolderRole:
            return world->folderName();
        case ModeRole:
            return world->gameType().toTranslatedString();
        case LastPlayedRole:
            return QVariant::fromValue<qint64>(world->lastPlayed().toMSecsSinceEpoch());
        case SizeRole:
            return QVariant::fromValue<qint64>(world->bytes());
        case IconRole:
            return world->iconFile().isEmpty() ? QString() : QUrl::fromLocalFile(world->iconFile()).toString();
        case SeedRole:
            return QString::number(world->seed());
        default:
            return {};
    }
}

void WorldsModel::play(int row)
{
    const World* world = worldAt(row);
    if (world && m_instance) {
        if (auto* quickPlay = QuickPlayBridge::instance()) {
            quickPlay->play(m_instance->id(), "world", world->folderName());
        } else {
            APPLICATION->launch(m_instance, LaunchMode::Normal, std::make_shared<MinecraftTarget>(MinecraftTarget::parse(world->folderName(), true)));
        }
    }
}

void WorldsModel::openFolder(int row) const
{
    if (const World* world = worldAt(row); world && m_instance) {
        DesktopServices::openPath(QDir(m_instance->gameRoot()).filePath("saves/" + world->folderName()), true);
    }
}

void WorldsModel::openWorldsFolder() const
{
    if (m_instance) {
        const QString path = QDir(m_instance->gameRoot()).filePath("saves");
        QDir().mkpath(path);
        DesktopServices::openPath(path, true);
    }
}

void WorldsModel::copySeed(int row) const
{
    if (const World* world = worldAt(row)) {
        QGuiApplication::clipboard()->setText(QString::number(world->seed()));
    }
}

bool WorldsModel::rename(int row, const QString& name)
{
    World* world = worldAt(row);
    return world && !name.trimmed().isEmpty() && world->rename(name.trimmed());
}

void WorldsModel::runTask(std::unique_ptr<Task> task)
{
    if (!task || m_busy) {
        return;
    }
    m_busy = true;
    emit busyChanged();
    m_worlds->stopWatching();
    Task* raw = task.release();
    connect(raw, &Task::failed, this, &WorldsModel::failed);
    connect(raw, &Task::finished, this, [this, raw] {
        raw->deleteLater();
        if (m_worlds) {
            m_worlds->startWatching();
            m_worlds->update();
        }
        m_busy = false;
        emit busyChanged();
    });
    raw->start();
}

void WorldsModel::duplicate(int row, const QString& name)
{
    if (m_worlds && worldAt(row) && !name.trimmed().isEmpty()) {
        runTask(m_worlds->createCopyWorldTask(row, name.trimmed()));
    }
}

void WorldsModel::remove(int row)
{
    if (m_worlds && worldAt(row)) {
        runTask(m_worlds->createDeleteWorldTask(row));
    }
}

void WorldsModel::refresh()
{
    if (m_worlds) {
        m_worlds->update();
    }
}
