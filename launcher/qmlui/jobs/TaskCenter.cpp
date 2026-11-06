#include "TaskCenter.h"

#include <QCoreApplication>

TaskCenter* TaskCenter::instance()
{
    static QPointer<TaskCenter> s;
    if (!s) {
        s = new TaskCenter(QCoreApplication::instance());
    }
    return s;
}

TaskCenter::TaskCenter(QObject* parent) : QAbstractListModel(parent) {}

void TaskCenter::track(const Task::Ptr& task, const QString& title, const QString& kind, const QString& instanceId, const QString& icon)
{
    if (!task || rowOf(task.get()) >= 0) {
        return;
    }
    Task* t = task.get();
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append({ t, task, title, task->getStatus(), kind, instanceId, icon, -1 });
    endInsertRows();
    m_revision++;
    emit changed();

    connect(t, &Task::status, this, [this, t](const QString& s) {
        const int row = rowOf(t);
        if (row >= 0 && !s.isEmpty()) {
            m_items[row].status = s;
            touch(row);
        }
    });
    connect(t, &Task::progress, this, [this, t](qint64 cur, qint64 total) {
        const int row = rowOf(t);
        if (row >= 0) {
            m_items[row].progress = total > 0 ? qBound(0.0, double(cur) / double(total), 1.0) : -1;
            touch(row);
        }
    });
    connect(t, &Task::abortStatusChanged, this, [this, t] {
        const int row = rowOf(t);
        if (row >= 0) {
            touch(row);
        }
    });
    connect(t, &Task::finished, this, [this, t] {
        const int row = rowOf(t);
        if (row < 0) {
            return;
        }
        const QString title = m_items[row].title;
        const bool ok = t->wasSuccessful();
        const bool aborted = t->getState() == Task::State::AbortedByUser;
        const QString reason = t->failReason();
        t->disconnect(this);
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
        m_revision++;
        emit changed();
        if (!aborted) {
            emit taskFinished(title, ok, reason);
        }
    });
}

int TaskCenter::rowOf(const Task* t) const
{
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].task == t) {
            return i;
        }
    }
    return -1;
}

void TaskCenter::touch(int row)
{
    m_revision++;
    emit dataChanged(index(row), index(row));
    emit changed();
}

int TaskCenter::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant TaskCenter::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size()) {
        return {};
    }
    const auto& it = m_items[index.row()];
    switch (role) {
        case TitleRole:
            return it.title;
        case StatusRole:
            return it.status;
        case ProgressRole:
            return it.progress;
        case KindRole:
            return it.kind;
        case InstanceRole:
            return it.instanceId;
        case IconRole:
            return it.icon;
        case CanCancelRole:
            return it.task->canAbort();
    }
    return {};
}

QHash<int, QByteArray> TaskCenter::roleNames() const
{
    return { { TitleRole, "title" },     { StatusRole, "status" },     { ProgressRole, "progress" }, { KindRole, "kind" },
             { InstanceRole, "instanceId" }, { IconRole, "icon" }, { CanCancelRole, "canCancel" } };
}

QString TaskCenter::title() const
{
    return m_items.isEmpty() ? QString() : m_items.first().title;
}

QString TaskCenter::status() const
{
    return m_items.isEmpty() ? QString() : m_items.first().status;
}

double TaskCenter::progress() const
{
    if (m_items.isEmpty()) {
        return -1;
    }
    double sum = 0;
    for (const auto& it : m_items) {
        if (it.progress < 0) {
            return -1;
        }
        sum += it.progress;
    }
    return sum / m_items.size();
}

void TaskCenter::cancel(int row)
{
    if (row >= 0 && row < m_items.size() && m_items[row].task->canAbort()) {
        m_items[row].task->abort();
    }
}

double TaskCenter::instanceProgress(const QString& instanceId) const
{
    for (const auto& it : m_items) {
        if (!instanceId.isEmpty() && it.instanceId == instanceId) {
            return it.progress;
        }
    }
    return -2;
}

QString TaskCenter::instanceStatus(const QString& instanceId) const
{
    for (const auto& it : m_items) {
        if (!instanceId.isEmpty() && it.instanceId == instanceId) {
            return it.status.isEmpty() ? it.title : it.status;
        }
    }
    return {};
}

QVariantList TaskCenter::ofKind(const QString& kind) const
{
    QVariantList out;
    for (int i = 0; i < m_items.size(); i++) {
        const auto& it = m_items[i];
        if (it.kind == kind) {
            out.append(QVariantMap{ { "row", i },
                                    { "title", it.title },
                                    { "status", it.status },
                                    { "progress", it.progress },
                                    { "icon", it.icon },
                                    { "canCancel", it.task->canAbort() } });
        }
    }
    return out;
}
