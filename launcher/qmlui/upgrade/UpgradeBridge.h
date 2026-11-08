// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include "tasks/Task.h"

#include "UpgradePlanTask.h"

class LauncherBridge;

/**
 * "Move to another Minecraft version" of a modded instance, exposed to QML as `Upgrade`.
 *
 * startPlan(instanceId, targetMc) looks up, for every installed mod, a version that works on the
 * target Minecraft with the same loader (UpgradePlanTask, a background job in TaskCenter) — nothing
 * is changed on disk. apply(plan, newName, choices) then copies the instance, switches the copy's
 * Minecraft/loader version, replaces the "ready" mods, disables the rest and writes a report. The
 * original instance is never touched.
 */
class UpgradeBridge : public QObject {
    Q_OBJECT
    /// The result of the last plan run: { state, error, instanceId, instanceName, mc, targetMc,
    /// delta, hasWorlds, loader, rows, counts } — empty while nothing was planned yet.
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)
    Q_PROPERTY(bool planning READ planning NOTIFY planChanged)
    Q_PROPERTY(bool applying READ applying NOTIFY applyChanged)
    /// { active, status, progress } of the running apply job
    Q_PROPERTY(QVariantMap applyJob READ applyJob NOTIFY applyChanged)

   public:
    explicit UpgradeBridge(LauncherBridge* launcher, QObject* parent = nullptr);

    QVariantMap plan() const { return m_plan; }
    bool planning() const;
    bool applying() const { return !m_applyTask.isNull(); }
    QVariantMap applyJob() const { return m_applyJob; }

    /// Look up replacement versions for every mod of the instance (background job).
    Q_INVOKABLE void startPlan(const QString& instanceId, const QString& targetMcVersion);
    Q_INVOKABLE void cancelPlan();
    /// plan: the Upgrade.plan object the dialog shows; choices: rowId -> index of the chosen
    /// alternative among its suggestions (only for "missing" rows).
    Q_INVOKABLE void apply(const QVariantMap& plan, const QString& newName, const QVariantMap& choices);
    Q_INVOKABLE void cancelApply();
    /// "<name> <version>" for the upgraded copy (also used by the unit tests).
    Q_INVOKABLE QString defaultName(const QString& instanceId, const QString& targetMcVersion) const;

   signals:
    void planChanged();
    void applyChanged();
    void applyFinished(bool ok, const QString& message, const QString& newInstanceName, const QString& newInstanceId);

   private:
    void planFinished();
    void startPlanTask(const QString& instanceId, const QString& targetMcVersion);

    QPair<QString, QString> m_pendingPlan;  // a version picked while another plan was running

    LauncherBridge* m_launcher;
    QVariantMap m_plan;
    QHash<QString, UpgradeRow> m_rowsById;   // full download info behind the plan shown to QML
    QPointer<UpgradePlanTask> m_planTask;
    Task::Ptr m_planTaskKeep;
    QPointer<Task> m_applyTask;
    QVariantMap m_applyJob;
};
