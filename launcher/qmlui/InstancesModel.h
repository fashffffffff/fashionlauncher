// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QSortFilterProxyModel>

class MinecraftInstance;

/**
 * QML-facing view of InstanceList: named roles, sorted by last played, filterable by text.
 */
class InstancesModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

   public:
    enum Roles {
        IdRole = Qt::UserRole + 100,
        NameRole,
        IconRole,
        GameVersionRole,
        LoaderRole,
        LastPlayedRole,
        PlayTimeRole,
        RunningRole,
        BackdropRole,
        GroupRole,
        NotesRole,
        PackNameRole,
        GameRootRole,
        InstanceRootRole,
    };

    explicit InstancesModel(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role) const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);
    int count() const { return rowCount(); }

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString& id) const;
    /// Forget cached version/screenshot info (e.g. when the window regains focus)
    Q_INVOKABLE void refreshMeta();

   signals:
    void filterChanged();
    void countChanged();

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

   private:
    struct Meta {
        QString gameVersion;
        QString loader;
        QString backdrop;
    };
    MinecraftInstance* instanceAt(const QModelIndex& index) const;
    const Meta& meta(MinecraftInstance* inst) const;

    QString m_filter;
    mutable QHash<QString, Meta> m_meta;
};
