// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariant>

class MinecraftInstance;

/**
 * Shared content store (hard links, hash-addressed, GC) and instance snapshots (last 5 launches, restore).
 * Exposed to QML as "Store". See ContentStore.h and Snapshots.h for the storage rules.
 */
class StoreBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool shareFiles READ shareFiles WRITE setShareFiles NOTIFY shareFilesChanged)
    Q_PROPERTY(double savedBytes READ savedBytes NOTIFY statsChanged)
    Q_PROPERTY(int sharedInstances READ sharedInstances NOTIFY statsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString restoring READ restoring NOTIFY restoringChanged)

   public:
    explicit StoreBridge(QObject* parent = nullptr);
    ~StoreBridge() override;

    // Called right before an instance is launched: links its files into the store and writes a snapshot.
    static void beforeLaunch(MinecraftInstance* inst);

    bool shareFiles() const;
    void setShareFiles(bool on);
    double savedBytes() const { return double(m_savedBytes); }
    int sharedInstances() const { return m_sharedInstances; }
    int revision() const { return m_revision; }
    QString restoring() const { return m_restoring; }

    // Snapshots of an instance, newest first, each compared with the instance as it is now. Answer: historyReady.
    Q_INVOKABLE void loadHistory(const QString& instanceId);
    // Puts the instance back to a snapshot. Answer: restoreFinished.
    Q_INVOKABLE bool restore(const QString& instanceId, const QString& snapshotFile);
    Q_INVOKABLE void refresh();

   signals:
    void shareFilesChanged();
    void statsChanged();
    void revisionChanged();
    void restoringChanged();
    void historyReady(const QString& instanceId, const QVariantList& snapshots);
    // result: { ok, restored, missing: [names], componentsChanged }
    void restoreFinished(const QString& instanceId, const QVariantMap& result);

   private:
    void snapshotNow(MinecraftInstance* inst);
    void rewatch();
    void schedulePass(const QString& instanceId = {});
    void runPass();
    void finishRestore(const QString& instanceId, QVariantMap result);
    void downloadMissing(const QString& instanceId, const QList<QJsonObject>& missing, QVariantMap result);

    static StoreBridge* s_self;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QSet<QString> m_pending;  // instance ids; empty with m_fullPass = everything
    bool m_fullPass = true;
    bool m_passRunning = false;
    bool m_passAgain = false;
    qint64 m_savedBytes = 0;
    int m_sharedInstances = 0;
    int m_revision = 0;
    QString m_restoring;
};
