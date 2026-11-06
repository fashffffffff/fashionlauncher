#pragma once

#include <QAbstractListModel>
#include <QPointer>

#include "tasks/Task.h"

/**
 * Every long-running job the QML shell starts (creating/importing instances, mod installs and updates, Java
 * downloads...) registers here, so one indicator can show it on every page. Exposed to QML as `Tasks`.
 */
class TaskCenter : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

   public:
    enum Roles { TitleRole = Qt::UserRole + 1, StatusRole, ProgressRole, KindRole, InstanceRole, IconRole, CanCancelRole };

    static TaskCenter* instance();

    // kind: "create", "resources", "java", "update", ... ; instanceId may be empty (e.g. for an instance being created).
    void track(const Task::Ptr& task, const QString& title, const QString& kind, const QString& instanceId = {},
               const QString& icon = {});

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool busy() const { return !m_items.isEmpty(); }
    // Summary of the first job ("Installing X"), plus the average progress of all jobs (-1 = unknown).
    QString title() const;
    QString status() const;
    double progress() const;
    int revision() const { return m_revision; }

    Q_INVOKABLE void cancel(int row);
    // -2 = nothing running for this instance, -1 = running with unknown progress, else 0..1.
    Q_INVOKABLE double instanceProgress(const QString& instanceId) const;
    Q_INVOKABLE QString instanceStatus(const QString& instanceId) const;
    Q_INVOKABLE QVariantList ofKind(const QString& kind) const;

   signals:
    void changed();
    void taskFinished(const QString& title, bool ok, const QString& error);

   private:
    explicit TaskCenter(QObject* parent = nullptr);
    struct Item {
        Task* task;
        Task::Ptr keep;
        QString title, status, kind, instanceId, icon;
        double progress = -1;
    };
    int rowOf(const Task* t) const;
    void touch(int row);

    QList<Item> m_items;
    int m_revision = 0;
};
