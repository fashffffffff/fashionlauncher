// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QIdentityProxyModel>
#include <QPointer>

class MinecraftInstance;
class WorldList;
class World;

/**
 * Worlds of one instance for QML (`WorldsModel { instanceId: ... }`), with the actions of the classic world page.
 */
class WorldsModel : public QIdentityProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString instanceId READ instanceId WRITE setInstanceId NOTIFY instanceIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

   public:
    enum Roles { NameRole = Qt::UserRole + 300, FolderRole, ModeRole, LastPlayedRole, SizeRole, IconRole, SeedRole };

    explicit WorldsModel(QObject* parent = nullptr);

    QString instanceId() const { return m_instanceId; }
    void setInstanceId(const QString& id);
    int count() const { return rowCount(); }
    bool busy() const { return m_busy; }

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role) const override;

    Q_INVOKABLE void play(int row);
    Q_INVOKABLE void openFolder(int row) const;
    Q_INVOKABLE void openWorldsFolder() const;
    Q_INVOKABLE void copySeed(int row) const;
    Q_INVOKABLE bool rename(int row, const QString& name);
    Q_INVOKABLE void duplicate(int row, const QString& name);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void refresh();

   signals:
    void instanceIdChanged();
    void countChanged();
    void busyChanged();
    void failed(const QString& reason);

   private:
    World* worldAt(int row) const;
    void runTask(std::unique_ptr<class Task> task);

    QString m_instanceId;
    QPointer<MinecraftInstance> m_instance;
    WorldList* m_worlds = nullptr;
    bool m_busy = false;
};
