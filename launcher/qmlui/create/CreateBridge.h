// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>

#include "modplatform/ModIndex.h"
#include "net/NetJob.h"
#include "tasks/Task.h"

class InstanceTask;
class MetaVersionModel;
class PackSearchModel;

/**
 * Creating instances: vanilla versions with mod loaders, modpacks from Modrinth/CurseForge, imports.
 * Exposed to QML as "Creator". The work itself is done by Prism's creation tasks
 * (VanillaCreationTask, InstanceImportTask) wrapped in InstanceList's staging task.
 */
class CreateBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* versions READ versions CONSTANT)
    Q_PROPERTY(QObject* loaderVersions READ loaderVersions CONSTANT)
    Q_PROPERTY(QObject* packs READ packs CONSTANT)

    Q_PROPERTY(QString loader READ loader NOTIFY loaderChanged)
    Q_PROPERTY(QString loaderName READ loaderName NOTIFY loaderChanged)
    Q_PROPERTY(QString loaderNote READ loaderNote NOTIFY loaderChanged)

    Q_PROPERTY(QVariantMap pack READ pack NOTIFY packChanged)
    Q_PROPERTY(QVariantList packVersions READ packVersions NOTIFY packChanged)
    Q_PROPERTY(bool packVersionsLoading READ packVersionsLoading NOTIFY packChanged)
    Q_PROPERTY(QString packError READ packError NOTIFY packChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY progressChanged)

    Q_PROPERTY(QString lastGroup READ lastGroup CONSTANT)
    Q_PROPERTY(QString autoTest READ autoTest CONSTANT)

   public:
    explicit CreateBridge(QObject* parent = nullptr);
    ~CreateBridge() override;

    QObject* versions() const;
    QObject* loaderVersions() const;
    QObject* packs() const;

    QString loader() const { return m_loader; }
    QString loaderName() const;
    QString loaderNote() const { return m_loaderNote; }

    QVariantMap pack() const { return m_packInfo; }
    QVariantList packVersions() const { return m_packVersions; }
    bool packVersionsLoading() const { return m_packJob != nullptr; }
    QString packError() const { return m_packError; }

    bool busy() const { return m_task != nullptr; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    bool canCancel() const;
    QString lastGroup() const;
    QString autoTest() const;

    /// Start loading the Minecraft version list (call when the page opens).
    Q_INVOKABLE void init();
    /// Pick the mod loader ("", "fabric", "quilt", "forge", "neoforge") for a Minecraft version.
    Q_INVOKABLE void setLoader(const QString& loader, const QString& mcVersion);
    /// Show versions of the pack at a row of the search results.
    Q_INVOKABLE void selectPack(int row);
    Q_INVOKABLE void clearPack();

    Q_INVOKABLE QStringList groups() const;
    Q_INVOKABLE QVariantList icons() const;
    /// Copy an image into the icon library; returns its key ("" on failure).
    Q_INVOKABLE QString addIconFile(const QString& pathOrUrl);
    /// Name suggested for an import source, "" when it can't be imported.
    Q_INVOKABLE QString importName(const QString& source) const;
    Q_INVOKABLE QString formatCount(double n) const;

    Q_INVOKABLE bool createVanilla(const QString& mcVersion, const QString& loaderVersion, const QString& name, const QString& icon,
                                   const QString& group);
    /// icon "" uses the pack's own logo.
    Q_INVOKABLE bool createModpack(int versionIndex, const QString& name, const QString& icon, const QString& group);
    Q_INVOKABLE bool createImport(const QString& source, const QString& name, const QString& icon, const QString& group);
    /// Instances found in other launchers (keys from Tools.launchers). link = share their game folder instead of copying.
    /// With one key the name applies to it; with several each keeps its own name.
    Q_INVOKABLE bool createFromLaunchers(const QVariantList& keys, bool link, const QString& name, const QString& icon,
                                         const QString& group);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();
    /// Automation (FFPRISM_CREATE): quits the app when FFPRISM_CREATE_QUIT=1.
    Q_INVOKABLE void finishAutoTest(bool ok);

   signals:
    void loaderChanged();
    void packChanged();
    void busyChanged();
    void progressChanged();
    void errorChanged();
    void created(const QString& instanceId, const QString& name);

   private:
    bool run(InstanceTask* task, const QString& name, const QString& originalName, const QString& version, const QString& icon,
             const QString& group);
    bool start(const Task::Ptr& task);
    void applyPackSettings(const QUrl& source);
    void setError(const QString& e);
    void fetchPackLogo();

    MetaVersionModel* m_versions;
    MetaVersionModel* m_loaderVersions;
    PackSearchModel* m_packs;

    QString m_loader;
    QString m_loaderNote;

    ModPlatform::IndexedPack::Ptr m_pack;
    QString m_packProvider;
    QVariantMap m_packInfo;
    QVariantList m_packVersions;
    QString m_packError;
    QString m_packLogoPath;
    Task::Ptr m_packJob;

    Task::Ptr m_task;
    QString m_status;
    double m_progress = -1;
    QString m_error;
    QString m_createdId;
    QString m_createdName;
    QUrl m_packSource;  // modpack being imported, for its fashion-launcher.json
};
