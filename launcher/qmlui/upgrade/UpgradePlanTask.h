// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "modplatform/ModIndex.h"
#include "tasks/Task.h"

#include "UpgradeLogic.h"

class ResourceFolderModel;
class MinecraftInstance;

/// An alternative mod suggestion (for a mod without a version for the target Minecraft).
struct UpgradeAlt {
    QString projectId;
    QString name;
    QString iconUrl;
    QString summary;
    ModPlatform::IndexedPack::Ptr pack;   // for the download task
    ModPlatform::IndexedVersion version;  // compatible version found at plan time
    bool hasVersion = false;              // a downloadable version exists for the target
};

/// One mod of the instance, as shown in the plan table.
struct UpgradeRow {
    QString id;             // unique key: file name of the installed mod
    QString name;           // readable name (metadata, jar contents or file name)
    QString fileName;       // current file in mods/ (as on disk, possibly *.disabled)
    QString provider;       // "modrinth" | "curseforge" | ""
    QString icon;           // remote project icon ("" when unknown)
    QString currentVersion; // what is installed now
    QString targetVersion;  // version number found for the target Minecraft ("" when missing)
    UpgradeLogic::Status status = UpgradeLogic::Status::Unknown;
    bool enabled = true;    // whether the installed file is currently enabled
    bool isDir = false;     // jar-in-jar folders have no downloadable version
    QString slug;           // metadata slug, for metadata cleanup during apply
    QString hash;           // metadata hash of the installed file
    QString hashFormat;     // and its algorithm
    ModPlatform::ModLoaderTypes modLoaders;  // loaders the metadata lists for this mod
    ModPlatform::IndexedPack::Ptr pack;      // minimal pack info for the download task
    ModPlatform::IndexedVersion target;      // full version info for the download task
    QList<UpgradeAlt> alts;                  // suggestions, only for status == Missing
};

/// What happens to the mod loader itself.
struct UpgradeLoaderPlan {
    QString key;             // "" | fabric | quilt | forge | neoforge ("" = no loader)
    QString name;            // display name ("Fabric", "NeoForge", ...)
    QString uid;             // Prism component uid ("" when none)
    QString currentVersion;  // installed loader version
    QString newVersion;      // version to set on the copy ("" = keep the current one)
    bool ok = true;          // the loader exists for the target Minecraft version
    bool checked = false;    // availability was actually verified, not assumed
};

/**
 * Asks Modrinth (and CurseForge where the mod's id is known) for a version of every installed mod
 * that works on the target Minecraft version with the same loader, and finds alternative mods for
 * the ones without one. Pure lookup: nothing on disk is changed. Phases run one after another;
 * the results are read through rows()/loaderPlan() once the task succeeds.
 */
class UpgradePlanTask : public Task {
    Q_OBJECT

   public:
    UpgradePlanTask(QString instanceId, QString targetMcVersion);
    ~UpgradePlanTask() override;

    bool abort() override;

    const QList<UpgradeRow>& rows() const { return m_rows; }
    const UpgradeLoaderPlan& loaderPlan() const { return m_loader; }
    const QString& instanceId() const { return m_instanceId; }
    const QString& targetMc() const { return m_targetMc; }
    QString currentMc() const { return m_currentMc; }
    bool hasWorlds() const { return m_hasWorlds; }

   protected:
    void executeTask() override;

   private:
    void scanFolder();
    void rowsScanned();
    void checkLoader();
    void loaderChecked();
    void queryModrinth();
    void queryFlame();
    void fetchIcons();
    void searchAlternatives();
    void fetchAltVersions();
    void finish();
    UpgradeRow* rowById(const QString& id);
    void setSub(const Task::Ptr& sub);
    /// Runs fn when `sub` finishes; ok=false means it failed (error is the fail reason).
    void onSubFinished(const Task::Ptr& sub, std::function<void(bool ok, const QString& error)> then);

    const QString m_instanceId;
    const QString m_targetMc;
    QList<UpgradeRow> m_rows;
    UpgradeLoaderPlan m_loader;
    QString m_currentMc;
    bool m_hasWorlds = false;
    bool m_aborted = false;
    QHash<QString, QString> m_hashesByRow;  // row id -> sha1 of the installed file
    int m_iconsWaiting = 0;

    QList<Task::Ptr> m_subs;  // sub tasks in flight (abort aborts them all)
    QPointer<ResourceFolderModel> m_folder;
    QPointer<MinecraftInstance> m_inst;
    std::shared_ptr<QJsonObject> m_latest;  // Modrinth version_files/update response, parsed per row
};
