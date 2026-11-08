// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "net/NetJob.h"
#include "tasks/Task.h"

class MinecraftInstance;
class ResourceFolderModel;

/**
 * Search results from Modrinth or CurseForge for one instance and kind, for QML (type "SearchModel").
 * Mods are filtered to the instance's Minecraft version and mod loader automatically.
 * Also loads versions of a project and installs one (with required dependencies for mods),
 * like the classic ResourceDownloadDialog but without dialogs.
 */
class SearchModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString instanceId READ instanceId WRITE setInstanceId NOTIFY instanceChanged)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    /// "modrinth" | "curseforge"
    Q_PROPERTY(QString platform READ platform WRITE setPlatform NOTIFY platformChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int sortIndex READ sortIndex WRITE setSortIndex NOTIFY sortIndexChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QVariantList sortOptions READ sortOptions NOTIFY platformChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY busyChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY busyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString gameVersion READ gameVersion NOTIFY instanceChanged)
    Q_PROPERTY(QString loaderText READ loaderText NOTIFY instanceChanged)
    Q_PROPERTY(bool hasModLoader READ hasModLoader NOTIFY instanceChanged)
    /// Row whose details are shown (-1 = none); selecting loads its versions
    /// Extra filters. side: "" | "client" | "server" (Modrinth only). mcFilter / loaderFilter: "" = the instance's,
    /// "any", or a Minecraft version / loader id ("fabric", "neoforge", …)
    Q_PROPERTY(QString side READ side WRITE setSide NOTIFY filtersChanged)
    Q_PROPERTY(QString mcFilter READ mcFilter WRITE setMcFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString loaderFilter READ loaderFilter WRITE setLoaderFilter NOTIFY filtersChanged)
    /// Release versions of Minecraft, newest first (loaded from metadata on first use)
    Q_PROPERTY(QStringList mcVersionChoices READ mcVersionChoices NOTIFY mcVersionChoicesChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedChanged)
    /// Projects ticked for one batch install ({ projectId, title, iconUrl }), kept across searches
    Q_PROPERTY(QVariantList picked READ picked NOTIFY pickedChanged)
    Q_PROPERTY(int pickedCount READ pickedCount NOTIFY pickedChanged)
    /// Projects still waiting in the batch queue (the one installing now not counted)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY pickedChanged)

   public:
    enum Roles {
        ProjectIdRole = Qt::UserRole + 300,
        TitleRole,
        AuthorRole,
        DownloadsRole,
        SummaryRole,
        IconUrlRole,
        UrlRole,
        StateRole,  // "" | "installing" | "installed"
        ProgressRole,
        PickedRole,
    };

    explicit SearchModel(QObject* parent = nullptr);
    ~SearchModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString instanceId() const { return m_instanceId; }
    void setInstanceId(const QString& id);
    QString kind() const { return m_kind; }
    void setKind(const QString& k);
    QString platform() const { return m_platform; }
    void setPlatform(const QString& p);
    QString query() const { return m_query; }
    void setQuery(const QString& q);
    int sortIndex() const { return m_sortIndex; }
    void setSortIndex(int i);
    QString category() const { return m_category; }
    void setCategory(const QString& c);
    QVariantList sortOptions() const;
    QVariantList categories() const { return m_categories; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    bool canLoadMore() const { return m_canLoadMore; }
    int count() const { return static_cast<int>(m_packs.size()); }
    QString gameVersion() const;
    QString loaderText() const;
    bool hasModLoader() const;
    QString side() const { return m_side; }
    void setSide(const QString& s);
    QString mcFilter() const { return m_mcFilter; }
    void setMcFilter(const QString& v);
    QString loaderFilter() const { return m_loaderFilter; }
    void setLoaderFilter(const QString& l);
    QStringList mcVersionChoices() const;
    int selectedRow() const { return m_selectedRow; }
    void setSelectedRow(int row);
    QVariantMap selected() const;

    Q_INVOKABLE void search();
    Q_INVOKABLE void loadMore();
    /** Installs a version of the project in row; versionIndex -1 picks the newest compatible one. */
    Q_INVOKABLE void install(int row, int versionIndex = -1);
    Q_INVOKABLE void openWebsite(int row) const;

    QVariantList picked() const;
    int pickedCount() const { return static_cast<int>(m_picked.size()); }
    int queuedCount() const { return static_cast<int>(m_queue.size()); }
    /** Ticks / unticks the project in row for the batch install. */
    Q_INVOKABLE void togglePick(int row);
    Q_INVOKABLE void unpick(const QString& projectId);
    Q_INVOKABLE void clearPicks();
    /** Installs every ticked project, one after another (shared dependencies are fetched once). */
    Q_INVOKABLE void installPicked();

   signals:
    void instanceChanged();
    void kindChanged();
    void platformChanged();
    void queryChanged();
    void sortIndexChanged();
    void categoryChanged();
    void categoriesChanged();
    void busyChanged();
    void countChanged();
    void selectedChanged();
    void pickedChanged();
    void filtersChanged();
    void mcVersionChoicesChanged();
    void message(const QString& text, bool isError);

   private:
    const ResourceAPI& api() const;
    bool isFlame() const { return m_platform == "curseforge"; }
    void scheduleSearch();
    void runSearch(bool append);
    void loadCategories();
    void loadVersions(int row, std::function<void()> then = {});
    void loadPackVersions(const ModPlatform::IndexedPack::Ptr& pack, std::function<void()> then, std::function<void()> failed = {});
    void runQueue();
    void queueStepDone(const QString& projectId);
    void startInstall(const ModPlatform::IndexedPack::Ptr& pack, const ModPlatform::IndexedVersion& version);
    void attachFolder();
    void refreshInstalled();
    QString stateOf(const QString& projectId) const;
    void notifyProject(const QString& projectId);
    int rowOf(const QString& projectId) const;

    QString m_instanceId;
    QString m_kind = "mods";
    QString m_platform = "modrinth";
    QString m_query;
    int m_sortIndex = 0;
    QString m_category;
    QVariantList m_categories;
    QPointer<MinecraftInstance> m_instance;
    QPointer<ResourceFolderModel> m_folder;

    QList<ModPlatform::IndexedPack::Ptr> m_packs;
    QHash<QString, qint64> m_downloads;
    int m_offset = 0;
    bool m_busy = false;
    bool m_canLoadMore = false;
    QString m_error;
    NetJob::Ptr m_searchJob;
    Task::Ptr m_categoryJob;
    int m_generation = 0;
    QTimer m_searchTimer;

    int m_selectedRow = -1;
    QHash<QString, Task::Ptr> m_versionJobs;  // project id -> running versions request
    QSet<QString> m_versionErrors;

    QSet<QString> m_installedIds;              // project ids installed in the instance
    QHash<QString, double> m_installing;       // project id -> progress (-1 = unknown)
    QHash<QString, Task::Ptr> m_installTasks;  // project id -> running task chain

    QString m_side;
    QString m_mcFilter;
    QString m_loaderFilter;
    mutable class MetaVersionModel* m_mcVersions = nullptr;

    QList<ModPlatform::IndexedPack::Ptr> m_picked;  // ticked, in the order they were ticked
    QList<ModPlatform::IndexedPack::Ptr> m_queue;   // batch install still to start
    QString m_queueCurrent;                         // project id the batch is installing now
};
