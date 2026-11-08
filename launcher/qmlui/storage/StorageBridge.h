// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QSet>
#include <QVariant>

#include "StorageScan.h"

/**
 * "Free up disk space": scans the data folder and instances in the background (tracked by TaskCenter), reports where
 * space goes and what can safely go, and removes the categories the user picks. Exposed to QML as "Storage".
 * The rules for what counts as removable live in StorageScan.h.
 */
class StorageBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged)
    Q_PROPERTY(bool cleaning READ cleaning NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap report READ report NOTIFY reportChanged)
    // Why cleaning is not possible right now ("" = it is): a game is running, other jobs are busy...
    Q_PROPERTY(QString blocker READ blocker NOTIFY blockerChanged)

   public:
    explicit StorageBridge(QObject* parent = nullptr);

    bool scanning() const { return m_scanning; }
    bool cleaning() const { return m_cleaning; }
    QVariantMap report() const { return m_report; }
    QString blocker() const;

    // logDays: logs and crash reports older than this count as old. Answer: reportChanged.
    Q_INVOKABLE void scan(int logDays, bool includeScreenshots);
    // Removes (or, for "duplicates", links into the store) the chosen categories of the last scan.
    // Answer: cleaned(freedBytes, failedCount), then a fresh scan.
    Q_INVOKABLE void clean(const QStringList& categoryIds);
    Q_INVOKABLE QVariantList files(const QString& categoryId, int limit = 300) const;
    Q_INVOKABLE void reveal(const QString& path) const;

   signals:
    void stateChanged();
    void reportChanged();
    void blockerChanged();
    void cleaned(double freedBytes, int failed);

   private:
    StorageScan::Input gather(int logDays, bool includeScreenshots) const;
    void debugHooks();

    bool m_scanning = false;
    bool m_cleaning = false;
    int m_logDays = 30;
    bool m_screenshots = false;
    StorageScan::Result m_last;
    QVariantMap m_report;
};
