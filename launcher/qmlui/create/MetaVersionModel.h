// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QPointer>

#include "meta/Version.h"
#include "meta/VersionList.h"
#include "tasks/Task.h"

/**
 * A filtered, QML-friendly view of one metadata version list (Minecraft, Fabric, Forge, ...).
 * Wraps Prism's Meta::VersionList; loading goes through the list's own load task.
 */
class MetaVersionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString uid READ uid NOTIFY uidChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filtersChanged)
    Q_PROPERTY(bool showSnapshots READ showSnapshots WRITE setShowSnapshots NOTIFY filtersChanged)
    Q_PROPERTY(bool showOld READ showOld WRITE setShowOld NOTIFY filtersChanged)
    /// Best default pick among the visible rows: the recommended one, else the newest.
    Q_PROPERTY(QString recommended READ recommended NOTIFY countChanged)

   public:
    enum Roles { VersionRole = Qt::UserRole + 1, TypeRole, DateRole, RecommendedRole };

    explicit MetaVersionModel(bool filterTypes, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString uid() const { return m_uid; }
    bool loading() const { return m_loadTask != nullptr; }
    QString error() const { return m_error; }
    QString search() const { return m_search; }
    void setSearch(const QString& s);
    bool showSnapshots() const { return m_showSnapshots; }
    void setShowSnapshots(bool on);
    bool showOld() const { return m_showOld; }
    void setShowOld(bool on);
    QString recommended() const;

    /// Point the model at a metadata list. A null parentFilter shows every version; an empty
    /// one plus forceEmpty=true shows nothing (loader not available for this Minecraft version).
    void setSource(const QString& uid, const QString& parentFilter = QString(), bool forceEmpty = false);
    Q_INVOKABLE void reload(bool force = true);
    Q_INVOKABLE int indexOf(const QString& version) const;

    /// The version as Prism's BaseVersion, for creation tasks.
    BaseVersion::Ptr find(const QString& version) const;

   signals:
    void uidChanged();
    void countChanged();
    void loadingChanged();
    void errorChanged();
    void filtersChanged();

   private:
    void rebuild();
    bool accepts(const Meta::Version::Ptr& v) const;

    bool m_filterTypes;
    QString m_uid;
    Meta::VersionList::Ptr m_list;
    QList<Meta::Version::Ptr> m_rows;
    QString m_parentFilter;
    bool m_forceEmpty = false;
    QString m_search;
    bool m_showSnapshots = false;
    bool m_showOld = false;
    QString m_error;
    Task::Ptr m_loadTask;
    QList<QMetaObject::Connection> m_listConnections;
};
