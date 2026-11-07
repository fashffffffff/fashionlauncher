// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

class MinecraftInstance;
class LaunchController;

/**
 * The crash doctor (QML: `Doctor`). Lives for the whole process — the QML window is closed while a game
 * runs — and notices when a game that was up exits abnormally. The QML dialog then shows `report`:
 *
 *   { instanceId, instanceName, source: "crash" | "log", logFile,
 *     findings: [CrashRules finding + fix annotation], excerpt, excerptStart, mentioned }
 *
 * Fix annotations on findings: `fix` = "install" (missing mod on Modrinth: installDependency) | "memory" |
 * "java" | "reinstall" (file has Modrinth/CurseForge metadata: reinstallFile) | "trash" (broken file without
 * metadata: trashFile) | "mods" (open the mods tab) | "" — plus `fileExists` for file findings.
 * Memory and Java fixes are done in QML through `Settings`, which already knows recommendedMaxMemory and the
 * installed Javas.
 */
class CrashDoctor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap report READ report NOTIFY reportChanged)
    /// Instance whose game crashed and whose report the user has not seen yet ("" = none)
    Q_PROPERTY(QString pendingInstance READ pendingInstance NOTIFY pendingChanged)

   public:
    static CrashDoctor* instance();

    QVariantMap report() const { return m_report; }
    QString pendingInstance() const { return m_pending.value("instanceId").toString(); }

    /// Show the pending crash (makes it the report); false if there is none
    Q_INVOKABLE bool takePending();
    /// Analyze an instance's log: the live log of its last launch, else logs/latest.log (+ a fresh crash report)
    Q_INVOKABLE bool analyzeInstance(const QString& instanceId);
    /// Analyze any log file for an instance (debug entry: FFPRISM_PAGE=crash:<id>:<path>)
    Q_INVOKABLE bool analyzeFile(const QString& instanceId, const QString& path);
    Q_INVOKABLE void copyText(const QString& text) const;

    /// Install the mod a finding says is missing, from Modrinth, only when a project's slug is exactly its id
    Q_INVOKABLE void installDependency(const QString& instanceId, const QString& modId);
    /// Download a broken mod file again from the URL and hash in its metadata
    Q_INVOKABLE void reinstallFile(const QString& instanceId, const QString& fileName);
    /// Move a broken mod file to the Trash
    Q_INVOKABLE bool trashFile(const QString& instanceId, const QString& fileName);

   signals:
    void reportChanged();
    void pendingChanged();
    /// A game crashed: the QML shell opens the dialog (also on its next start if it was closed)
    void crashed(const QString& instanceId);
    /// key = "install:<modId>" | "reinstall:<file>"
    void fixFinished(const QString& key, bool ok, const QString& message);

   private:
    explicit CrashDoctor(QObject* parent = nullptr);
    void watch(MinecraftInstance* instance, LaunchController* controller);
    QVariantMap build(const QString& instanceId, const QString& text, const QString& source, const QString& logFile) const;
    QString liveLog(const QString& instanceId) const;

    QVariantMap m_report;
    QVariantMap m_pending;
};
