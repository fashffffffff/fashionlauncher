// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QUrl>
#include <functional>
#include <vector>

#include "Version.h"
#include "modplatform/CheckUpdateTask.h"
#include "tasks/Task.h"

class MinecraftInstance;
class ResourceFolderModel;
class Resource;

/**
 * Installed mods / resource packs / shaders of one instance, for QML (type "InstalledModel").
 * Wraps Prism's folder model of that kind (watching the folder while alive), adds search filtering,
 * enable/remove, drag-and-drop install, and the update check + update flow of the classic
 * ModFolderPage/ResourceUpdateDialog without any dialogs.
 */
class InstalledModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString instanceId READ instanceId WRITE setInstanceId NOTIFY sourceChanged)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY sourceChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString folderPath READ folderPath NOTIFY sourceChanged)
    Q_PROPERTY(bool hasModLoader READ hasModLoader NOTIFY sourceChanged)
    /// "" when idle, "checking" or "updating" while working
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY progressChanged)
    /// true once an update check finished for the current contents
    Q_PROPERTY(bool checked READ checked NOTIFY updatesChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updatesChanged)

   public:
    enum Roles {
        NameRole = Qt::UserRole + 200,
        VersionRole,
        AuthorRole,
        FileNameRole,
        EnabledRole,
        IconRole,
        DescriptionRole,
        ProviderRole,
        HomepageRole,
        NewVersionRole,
        IssuesRole,
        IsFolderRole,
    };

    explicit InstalledModel(QObject* parent = nullptr);
    ~InstalledModel() override;

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role) const override;

    QString instanceId() const { return m_instanceId; }
    void setInstanceId(const QString& id);
    QString kind() const { return m_kind; }
    void setKind(const QString& kind);
    QString filter() const { return m_filter; }
    void setFilter(const QString& f);
    int count() const { return rowCount(); }
    int totalCount() const;
    bool loading() const { return m_loading; }
    QString folderPath() const;
    bool hasModLoader() const;
    QString busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString statusText() const { return m_statusText; }
    bool checked() const { return m_checked; }
    int updateCount() const { return static_cast<int>(m_updates.size()); }

    Q_INVOKABLE void setItemEnabled(int row, bool enabled);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void openFolder() const;
    Q_INVOKABLE void openHomepage(int row) const;
    /** Copies the given local files into the folder; returns how many were accepted. */
    Q_INVOKABLE int addFiles(const QVariantList& urls);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void checkUpdates();
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void cancel();

   signals:
    void sourceChanged();
    void filterChanged();
    void countChanged();
    void loadingChanged();
    void busyChanged();
    void progressChanged();
    void updatesChanged();
    /** Something to tell the user (shown as a toast). */
    void message(const QString& text, bool isError);

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

   private:
    void attach();
    void detach();
    Resource* resourceAt(int proxyRow) const;
    void setBusy(const QString& b);
    void runTask(const Task::Ptr& task, std::function<void(bool ok, const QString& error)> done);
    void startChecks();
    void finishChecks();
    void clearUpdates();

    QString m_instanceId;
    QString m_kind;
    QString m_filter;
    QPointer<MinecraftInstance> m_instance;
    QPointer<ResourceFolderModel> m_folder;
    bool m_loading = false;

    QString m_busy;
    double m_progress = -1;
    QString m_statusText;
    Task::Ptr m_task;
    bool m_checked = false;

    // update check state; the check tasks keep references to these lists
    QList<Resource*> m_modrinthList;
    QList<Resource*> m_flameList;
    QList<Resource*> m_retryFlame;
    std::vector<Version> m_versions;
    shared_qobject_ptr<CheckUpdateTask> m_modrinthCheck;
    shared_qobject_ptr<CheckUpdateTask> m_flameCheck;
    std::vector<CheckUpdateTask::Update> m_updates;
    QHash<QString, QString> m_newVersionById;  // resource internal id -> new version
};
