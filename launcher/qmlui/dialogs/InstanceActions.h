// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

class Task;

/**
 * Copy, export, delete and icon changes for the QML dialogs (`InstanceActions` in QML).
 *
 * One job runs at a time; `job` is { active, title, status, progress (0..1 or -1) } and
 * `jobFinished(ok, message)` reports the result.
 */
class InstanceActions : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap job READ job NOTIFY jobChanged)

   public:
    explicit InstanceActions(QObject* parent = nullptr);

    QVariantMap job() const { return m_job; }

    /// Groups that exist, for the group field
    Q_INVOKABLE QStringList groups() const;
    /// { name, group, icon }
    Q_INVOKABLE QVariantMap copyDefaults(const QString& id) const;
    /// options: name, group, icon, saves, keepPlaytime, options, resourcePacks, shaderPacks, servers, mods, screenshots, links
    Q_INVOKABLE bool copy(const QString& id, const QVariantMap& options);

    /// "" if the instance can be deleted, else why not
    Q_INVOKABLE QString deleteProblem(const QString& id) const;
    /// Names of instances that link to this one's files
    Q_INVOKABLE QStringList linkedInstances(const QString& id) const;
    /// Moves the instance to the trash (or deletes it where there is none). Returns true if it went to the trash.
    Q_INVOKABLE bool remove(const QString& id);

    Q_INVOKABLE QString iconKey(const QString& id) const;
    Q_INVOKABLE void setIcon(const QString& id, const QString& key);

    /// format: "zip" | "modrinth" | "curseforge". Returns { name, version, author, summary, optionalFiles, recommendedRam, problem }
    Q_INVOKABLE QVariantMap exportDefaults(const QString& id, const QString& format) const;
    /// Top-level files and folders that can go into the export: [{ path, name, isDir, included }]
    Q_INVOKABLE QVariantList exportEntries(const QString& id, const QString& format) const;
    Q_INVOKABLE QString exportFileName(const QString& id, const QString& format, const QString& name) const;
    /// excluded: entry paths to leave out (remembered in .packignore). output: path or file:// URL.
    Q_INVOKABLE bool exportTo(const QString& id, const QString& format, const QString& output, const QVariantMap& options,
                              const QStringList& excluded);

    Q_INVOKABLE void cancelJob();

   signals:
    void jobChanged();
    void jobFinished(bool ok, const QString& message);

   private:
    bool runJob(Task* task, const QString& title, const QString& doneMessage);
    QVariantMap m_job;
    QPointer<Task> m_task;
};
