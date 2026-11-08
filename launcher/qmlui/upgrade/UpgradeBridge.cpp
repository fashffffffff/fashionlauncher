// SPDX-License-Identifier: GPL-3.0-only
#include "UpgradeBridge.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceCopyPrefs.h"
#include "InstanceCopyTask.h"
#include "InstanceList.h"
#include "ResourceDownloadTask.h"
#include "qmlui/LauncherBridge.h"
#include "minecraft/mod/Resource.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "qmlui/jobs/TaskCenter.h"
#include "qmlui/resources/ResourceCommon.h"
#include "tasks/ConcurrentTask.h"

/// The full apply job: copy -> switch version -> replace mods -> disable the rest -> report.
/// The copy itself is Prism's InstanceCopyTask wrapped in InstanceList's staging task; the mod
/// replacement reuses ResourceDownloadTask. The whole job is tracked in TaskCenter (kind "upgrade").
/// UpgradeBridge (a friend) sets up the context fields and reads the result.
class UpgradeApplyTask : public Task {
   public:
    struct Request {
        QString instanceId;  // the ORIGINAL instance (it is only read, never written)
        QString targetMc;
        QString newName;
        UpgradeLoaderPlan loader;
        QList<UpgradeRow> rows;
        QVariantMap choices;  // rowId -> alternative index (int), for "missing" rows
    };

    explicit UpgradeApplyTask(Request req) : Task(), m_req(std::move(req)) {}
    ~UpgradeApplyTask() override
    {
        for (const auto& sub : m_subs) {
            if (sub) {
                sub->disconnect(this);
            }
        }
    }

    bool abort() override
    {
        m_aborted = true;
        for (const auto& sub : m_subs) {
            if (sub) {
                sub->abort();
            }
        }
        return Task::abort();
    }

    QString newInstanceId() const { return m_newId; }

   protected:
    void executeTask() override { startCopy(); }

   private:
    /// Propagates a sub task's status/progress into a slice of this task's 0..1 range.
    void trackSub(const Task::Ptr& sub, double from, double to)
    {
        m_subs.append(sub);
        connect(sub.get(), &Task::status, this, [this, sub](const QString& s) {
            if (!s.isEmpty()) {
                setStatus(m_phase + " — " + s);
            }
        });
        connect(sub.get(), &Task::progress, this, [this, from, to](qint64 cur, qint64 total) {
            if (total > 0) {
                const double frac = qBound(0.0, from + (double(cur) / double(total)) * (to - from), 1.0);
                setProgress(qRound64(frac * 1000), 1000);
            }
        });
    }

    void startCopy()
    {
        auto* orig = QmlResources::findInstance(m_req.instanceId);
        if (!orig) {
            emitFailed(tr("This instance no longer exists."));
            return;
        }
        if (orig->isRunning()) {
            emitFailed(tr("Close the game before moving it to another version."));
            return;
        }
        m_newName = m_req.newName.trimmed().isEmpty() ? UpgradeLogic::defaultName(orig->name(), m_req.targetMc) : m_req.newName.trimmed();

        // The same copy path as "Copy instance": mods, worlds, options, packs, servers, screenshots.
        InstanceCopyPrefs prefs;
        prefs.enableCopySaves(true);
        prefs.enableKeepPlaytime(true);
        prefs.enableCopyGameOptions(true);
        prefs.enableCopyResourcePacks(true);
        prefs.enableCopyShaderPacks(true);
        prefs.enableCopyServers(true);
        prefs.enableCopyMods(true);
        prefs.enableCopyScreenshots(true);

        auto* copyTask = new InstanceCopyTask(orig, prefs);
        copyTask->setName(m_newName);
        copyTask->setGroup(APPLICATION->instances()->getInstanceGroup(m_req.instanceId));
        copyTask->setIcon(orig->iconKey());

        m_phase = tr("Copying the instance");
        setStatus(m_phase);
        setProgress(0, 1000);
        auto staging = Task::Ptr(APPLICATION->instances()->wrapInstanceTask(copyTask));
        m_selectConn = connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this,
                               [this](const QString& id) { m_newId = id; });
        trackSub(staging, 0.0, 0.6);
        connect(staging.get(), &Task::finished, this, [this, staging] {
            m_subs.removeAll(staging);
            if (m_aborted || isFinished()) {
                return;
            }
            disconnect(m_selectConn);
            if (!staging->wasSuccessful()) {
                emitFailed(staging->failReason().isEmpty() ? tr("The copy could not be created.") : staging->failReason());
                return;
            }
            if (!QmlResources::findInstance(m_newId)) {
                emitFailed(tr("The copy could not be created."));
                return;
            }
            setVersion();
        });
        staging->start();
    }

    /// The exact component surgery of LauncherBridge::changeVersion, through that code path.
    void setVersion()
    {
        m_phase = tr("Switching the version");
        setStatus(m_phase);
        setProgress(600, 1000);
        const QString loaderVersion = m_req.loader.newVersion.isEmpty() ? m_req.loader.currentVersion : m_req.loader.newVersion;
        const QString error = m_launcher->changeVersion(m_newId, m_req.targetMc, m_req.loader.key, loaderVersion);
        if (!error.isEmpty()) {
            emitFailed(error);
            return;
        }
        modsPhase();
    }

    void modsPhase()
    {
        m_phase = tr("Replacing mods");
        setStatus(m_phase);
        setProgress(650, 1000);
        auto* copy = QmlResources::findInstance(m_newId);
        if (!copy) {
            return finishOff(nullptr);
        }
        auto* folder = copy->loaderModList();
        if (!folder) {
            return finishOff(nullptr);
        }
        m_copyFolder = folder;
        // The copy's mods folder must be scanned before files can be disabled by index.
        connect(folder, &ResourceFolderModel::updateFinished, this, &UpgradeApplyTask::copyModsScanned, Qt::UniqueConnection);
        if (!folder->update() && !folder->empty()) {
            copyModsScanned();  // the rows are already there (the folder was scanned before)
        }
    }

    void copyModsScanned()
    {
        if (m_aborted || isFinished() || m_copyFolder.isNull()) {
            return;
        }
        disconnect(m_copyFolder, &ResourceFolderModel::updateFinished, this, &UpgradeApplyTask::copyModsScanned);

        auto downloads =
            makeShared<ConcurrentTask>(tr("Replacing mods"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
        int downloadsAdded = 0;
        const QDir modsDir(m_copyFolder->dir().absolutePath());
        for (const auto& row : m_req.rows) {
            if (row.status == UpgradeLogic::Status::Ready && !row.isDir && row.pack) {
                // A replaced mod with a new file name leaves its old file behind (the download task
                // only uninstalls the old file through a fully loaded model): remove it here.
                if (!row.target.fileName.isEmpty() && row.target.fileName != row.fileName) {
                    for (const QString& old : { row.fileName, row.fileName + ".disabled" }) {
                        const QString path = modsDir.absoluteFilePath(old);
                        if (QFileInfo::exists(path)) {
                            FS::deletePath(path);
                        }
                    }
                }
                downloads->addTask(makeShared<ResourceDownloadTask>(row.pack, row.target, m_copyFolder, true, "upgrade"));
                ++downloadsAdded;
            } else if (row.status == UpgradeLogic::Status::Missing && row.alts.size() > 0) {
                // A chosen suggestion is installed next to the disabled original.
                const int altIndex = m_req.choices.value(row.id, -1).toInt();
                if (altIndex >= 0 && altIndex < row.alts.size() && row.alts.at(altIndex).hasVersion) {
                    const auto& alt = row.alts.at(altIndex);
                    downloads->addTask(makeShared<ResourceDownloadTask>(alt.pack, alt.version, m_copyFolder, true, "upgrade"));
                    ++downloadsAdded;
                }
            }
        }
        if (downloadsAdded == 0) {
            return finishOff(m_copyFolder);
        }
        trackSub(downloads, 0.65, 0.9);
        connect(downloads.get(), &Task::finished, this, [this, downloads] {
            m_subs.removeAll(downloads);
            if (!m_aborted && !isFinished()) {
                finishOff(m_copyFolder);
            }
        });
        downloads->start();
    }

    void finishOff(ResourceFolderModel* folder)
    {
        m_phase = tr("Finishing up");
        setStatus(m_phase);
        setProgress(900, 1000);
        disableLeftovers(folder);
        writeReport();
        setProgress(1000, 1000);
        emitSucceeded();
    }

    /// Missing and unknown mods (and mods that were disabled all along) are turned off by renaming
    /// to *.disabled — never deleted.
    void disableLeftovers(ResourceFolderModel* folder)
    {
        if (!folder) {
            return;
        }
        QModelIndexList disable;
        for (const auto& row : m_req.rows) {
            const bool shouldDisable = row.status == UpgradeLogic::Status::Missing || row.status == UpgradeLogic::Status::Unknown
                                       || (row.status == UpgradeLogic::Status::Ready && !row.enabled);
            if (!shouldDisable) {
                continue;
            }
            for (int i = 0; i < static_cast<int>(folder->size()); ++i) {
                if (folder->at(i).fileinfo().fileName() == row.fileName) {
                    disable << folder->index(i, 0);
                    break;
                }
            }
        }
        if (!disable.isEmpty()) {
            folder->setResourceEnabled(disable, EnableAction::DISABLE);
        }
    }

    void writeReport()
    {
        auto* copy = QmlResources::findInstance(m_newId);
        if (!copy) {
            return;
        }
        int ready = 0, missing = 0, unknown = 0, suggestions = 0;
        for (const auto& row : m_req.rows) {
            switch (row.status) {
                case UpgradeLogic::Status::Ready:
                    ++ready;
                    break;
                case UpgradeLogic::Status::Missing:
                    ++missing;
                    if (m_req.choices.value(row.id, -1).toInt() >= 0) {
                        ++suggestions;
                    }
                    break;
                case UpgradeLogic::Status::Unknown:
                    ++unknown;
                    break;
            }
        }
        const QString loaderLine = m_req.loader.key.isEmpty()
                                       ? QString()
                                       : QStringLiteral(", %1 %2").arg(m_req.loader.name,
                                                                       m_req.loader.newVersion.isEmpty() ? m_req.loader.currentVersion
                                                                                                          : m_req.loader.newVersion);
        QString text;
        text += tr("Fashion Launcher — instance upgrade report") + "\n";
        text += "\n" + tr("Upgraded copy of: %1").arg(m_origName) + "\n";
        text += tr("From: Minecraft %1%2").arg(m_origMc, m_origLoader.isEmpty() ? QString() : ", " + m_origLoader) + "\n";
        text += tr("To: Minecraft %1%2").arg(m_req.targetMc, loaderLine) + "\n";
        text += tr("Created: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)) + "\n";
        text += "\n"
              + tr("%1 mod(s): %2 replaced, %3 disabled (no version for the new Minecraft), %4 disabled (unknown mod), %5 suggestion(s) installed")
                    .arg(m_req.rows.size())
                    .arg(ready)
                    .arg(missing)
                    .arg(unknown)
                    .arg(suggestions)
              + "\n";
        text += tr("The original instance was not changed.") + "\n";
        if (m_delta == UpgradeLogic::Delta::Upgrade) {
            text += "\n"
                  + tr("Worlds were copied as they are. Opening a world in a newer Minecraft version converts it permanently — "
                       "it cannot be opened in the older version afterwards.")
                  + "\n";
        }
        text += "\n";
        for (const auto& row : m_req.rows) {
            if (row.status == UpgradeLogic::Status::Ready) {
                text += QStringLiteral("- %1 (%2): %3 -> %4").arg(row.name, row.fileName, row.currentVersion, row.targetVersion) + "\n";
            } else if (row.status == UpgradeLogic::Status::Missing) {
                const int i = m_req.choices.value(row.id, -1).toInt();
                if (i >= 0 && i < row.alts.size()) {
                    text += tr("- %1 (%2): disabled; suggestion installed instead: %3").arg(row.name, row.fileName, row.alts.at(i).name) + "\n";
                } else {
                    text += tr("- %1 (%2): disabled — no version for Minecraft %3").arg(row.name, row.fileName, m_req.targetMc) + "\n";
                }
            } else {
                text += tr("- %1 (%2): left out — version unknown (no metadata)").arg(row.name, row.fileName) + "\n";
            }
        }
        FS::write(FS::PathCombine(copy->instanceRoot(), "UPGRADE-REPORT.txt"), text.toUtf8());
    }

    struct Request m_req;
    QList<Task::Ptr> m_subs;
    QMetaObject::Connection m_selectConn;
    QPointer<ResourceFolderModel> m_copyFolder;
    LauncherBridge* m_launcher = nullptr;  // set by UpgradeBridge (friend) before start()
    QString m_newId;
    QString m_newName;
    QString m_phase;
    QString m_origName;
    QString m_origMc;
    QString m_origLoader;
    UpgradeLogic::Delta m_delta = UpgradeLogic::Delta::Unknown;
    bool m_aborted = false;

    friend class UpgradeBridge;
};

UpgradeBridge::UpgradeBridge(LauncherBridge* launcher, QObject* parent) : QObject(parent), m_launcher(launcher) {}

bool UpgradeBridge::planning() const
{
    return m_planTask != nullptr;
}

void UpgradeBridge::startPlanTask(const QString& instanceId, const QString& targetMcVersion)
{
    auto task = Task::Ptr(new UpgradePlanTask(instanceId, targetMcVersion));
    m_planTask = static_cast<UpgradePlanTask*>(task.get());
    m_planTaskKeep = task;
    connect(task.get(), &Task::status, this, [this, t = task.get()](const QString& s) {
        if (m_planTask == t && !s.isEmpty()) {
            m_plan["status"] = s;
            emit planChanged();
        }
    });
    connect(task.get(), &Task::finished, this, &UpgradeBridge::planFinished);
    TaskCenter::instance()->track(task, tr("Planning the move to Minecraft %1").arg(targetMcVersion), "upgrade", instanceId);
    task->start();
}

// ---- plan -------------------------------------------------------------------------------------

void UpgradeBridge::startPlan(const QString& instanceId, const QString& targetMcVersion)
{
    if (m_planTask) {
        // Another version picked while planning: cancel the running lookup, then restart here.
        if (m_plan.value("targetMc").toString() == targetMcVersion && m_plan.value("instanceId").toString() == instanceId) {
            return;
        }
        m_pendingPlan = { instanceId, targetMcVersion };
        cancelPlan();
        return;
    }
    m_plan = { { "state", "loading" }, { "instanceId", instanceId }, { "targetMc", targetMcVersion }, { "status", QString() } };
    emit planChanged();

    startPlanTask(instanceId, targetMcVersion);
}

void UpgradeBridge::planFinished()
{
    auto* upgradePlan = m_planTask.data();
    if (!upgradePlan) {
        return;
    }
    m_planTaskKeep.reset();
    m_planTask.clear();
    upgradePlan->deleteLater();
    const QString instanceId = upgradePlan->instanceId();
    const QString targetMc = upgradePlan->targetMc();
    if (upgradePlan->getState() == Task::State::AbortedByUser && !m_pendingPlan.first.isEmpty()) {
        // A different version was picked while planning: start that one now.
        const auto pending = m_pendingPlan;
        m_pendingPlan = {};
        m_plan = { { "state", "loading" }, { "instanceId", pending.first }, { "targetMc", pending.second }, { "status", QString() } };
        emit planChanged();
        startPlanTask(pending.first, pending.second);
        return;
    }
    m_pendingPlan = {};
    const bool aborted = upgradePlan->getState() == Task::State::AbortedByUser;
    if (upgradePlan->wasSuccessful()) {
        QVariantList rows;
        int ready = 0, missing = 0, unknown = 0;
        m_rowsById.clear();
        for (const auto& row : upgradePlan->rows()) {
            QVariantList alts;
            for (const auto& alt : row.alts) {
                alts << QVariantMap{ { "projectId", alt.projectId },
                                     { "name", alt.name },
                                     { "icon", alt.iconUrl },
                                     { "summary", alt.summary },
                                     { "version", alt.version.versionNumber } };
            }
            rows << QVariantMap{ { "id", row.id },
                                 { "name", row.name },
                                 { "fileName", row.fileName },
                                 { "provider", row.provider },
                                 { "icon", row.icon },
                                 { "currentVersion", row.currentVersion },
                                 { "targetVersion", row.targetVersion },
                                 { "status", QString::fromLatin1(UpgradeLogic::statusKey(row.status)) },
                                 { "enabled", row.enabled },
                                 { "suggestions", alts } };
            m_rowsById.insert(row.id, row);
            switch (row.status) {
                case UpgradeLogic::Status::Ready:
                    ++ready;
                    break;
                case UpgradeLogic::Status::Missing:
                    ++missing;
                    break;
                case UpgradeLogic::Status::Unknown:
                    ++unknown;
                    break;
            }
        }
        const auto loader = upgradePlan->loaderPlan();
        m_plan = { { "state", "done" },
                   { "instanceId", upgradePlan->instanceId() },
                   { "targetMc", upgradePlan->targetMc() },
                   { "status", QString() },
                   { "mc", upgradePlan->currentMc() },
                   { "hasWorlds", upgradePlan->hasWorlds() },
                   { "delta", UpgradeLogic::compareVersions(upgradePlan->currentMc(), upgradePlan->targetMc()) == UpgradeLogic::Delta::Upgrade
                                  ? "upgrade"
                                  : (UpgradeLogic::compareVersions(upgradePlan->currentMc(), upgradePlan->targetMc())
                                                                                         == UpgradeLogic::Delta::Downgrade
                                         ? "downgrade"
                                         : "same") },
                   { "rows", rows },
                   { "counts", QVariantMap{ { "ready", ready }, { "missing", missing }, { "unknown", unknown }, { "total", rows.size() } } },
                   { "loader", QVariantMap{ { "key", loader.key },
                                            { "name", loader.name },
                                            { "currentVersion", loader.currentVersion },
                                            { "newVersion", loader.newVersion },
                                            { "ok", loader.ok },
                                            { "checked", loader.checked } } } };
    } else {
        m_plan = { { "state", aborted ? "cancelled" : "error" },
                   { "instanceId", instanceId },
                   { "targetMc", targetMc },
                   { "error", upgradePlan->failReason() } };
    }
    emit planChanged();
}

void UpgradeBridge::cancelPlan()
{
    if (m_planTask) {
        m_planTask->abort();
    }
}

// ---- apply ------------------------------------------------------------------------------------

void UpgradeBridge::apply(const QVariantMap& plan, const QString& newName, const QVariantMap& choices)
{
    if (!m_applyTask.isNull() || plan.value("state").toString() != "done") {
        return;
    }
    const QString instanceId = plan.value("instanceId").toString();
    const QString targetMc = plan.value("targetMc").toString();
    auto* orig = QmlResources::findInstance(instanceId);
    if (!orig) {
        emit applyFinished(false, tr("This instance no longer exists."), QString(), QString());
        return;
    }
    if (orig->isRunning()) {
        emit applyFinished(false, tr("Close the game before moving it to another version."), QString(), QString());
        return;
    }

    const QVariantMap loader = plan.value("loader").toMap();
    if (!loader.value("ok").toBool()) {
        emit applyFinished(false, tr("The mod loader does not support Minecraft %1.").arg(targetMc), QString(), QString());
        return;
    }

    UpgradeApplyTask::Request req;
    req.instanceId = instanceId;
    req.targetMc = targetMc;
    req.newName = newName;
    req.choices = choices;
    req.loader.key = loader.value("key").toString();
    req.loader.name = loader.value("name").toString();
    req.loader.currentVersion = loader.value("currentVersion").toString();
    req.loader.newVersion = loader.value("newVersion").toString();
    req.loader.ok = loader.value("ok").toBool();
    req.loader.checked = loader.value("checked").toBool();
    // The dialog's rows point back at the full rows (with download info) kept on the C++ side.
    for (const auto& v : plan.value("rows").toList()) {
        if (auto it = m_rowsById.constFind(v.toMap().value("id").toString()); it != m_rowsById.constEnd()) {
            req.rows.append(it.value());
        }
    }

    auto* task = new UpgradeApplyTask(std::move(req));
    m_applyTask = task;
    task->m_launcher = m_launcher;
    task->m_origName = orig->name();
    task->m_origMc = QmlResources::gameVersion(orig);
    task->m_origLoader = QmlResources::loaderNames(orig);
    task->m_delta = UpgradeLogic::compareVersions(task->m_origMc, targetMc);

    m_applyJob = { { "active", true }, { "status", QString() }, { "progress", -1 } };
    emit applyChanged();
    connect(task, &Task::status, this, [this, task](const QString& s) {
        if (m_applyTask == task && !s.isEmpty()) {
            m_applyJob["status"] = s;
            emit applyChanged();
        }
    });
    connect(task, &Task::progress, this, [this, task](qint64 cur, qint64 total) {
        if (m_applyTask == task) {
            m_applyJob["progress"] = total > 0 ? double(cur) / double(total) : -1.0;
            emit applyChanged();
        }
    });
    // TaskCenter's Task::Ptr owns the task (deleteLater); the lambda only borrows it.
    connect(task, &Task::finished, this, [this, t = task] {
        const bool ok = t->wasSuccessful();
        const QString name = t->m_newName;
        const QString newId = t->newInstanceId();
        const QString error = t->failReason();
        m_applyTask.clear();
        m_applyJob.clear();
        emit applyChanged();
        emit applyFinished(ok, ok ? tr("Upgraded copy “%1” created.").arg(name)
                                  : (error.isEmpty() ? tr("The upgrade was cancelled.") : error),
                           ok ? name : QString(), ok ? newId : QString());
    });
    TaskCenter::instance()->track(Task::Ptr(task), tr("Upgrading “%1” to Minecraft %2").arg(orig->name(), targetMc), "upgrade", instanceId,
                                  orig->iconKey());
    task->start();
}

void UpgradeBridge::cancelApply()
{
    if (m_applyTask && m_applyTask->canAbort()) {
        m_applyTask->abort();
    }
}

QString UpgradeBridge::defaultName(const QString& instanceId, const QString& targetMcVersion) const
{
    auto* inst = QmlResources::findInstance(instanceId);
    return UpgradeLogic::defaultName(inst ? inst->name() : QString(), targetMcVersion);
}
