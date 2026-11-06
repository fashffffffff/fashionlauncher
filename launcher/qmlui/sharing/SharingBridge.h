// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QVariant>

#include "tasks/Task.h"

/**
 * Instance sharing: pack manifests (mods by platform version id, side info), share links/files, applying a manifest to a
 * new or existing instance with server/client filters and exclusions. Exposed to QML as "Sharing".
 *
 * Manifest (also the .fashpack file):
 *   { format: 1, name, mc, loader, loaderVersion, includeClient,
 *     mods|resourcepacks|shaderpacks: [{ key, name, file, sha1, side: client|server|both|unknown,
 *                                        platform: modrinth|curseforge|local, projectId, versionId, size, url, slug }] }
 * Aliases for the multiplayer screen: manifest.minecraft (= mc), item.id (= key), match.missingCount.
 * key is "modrinth:<projectId>", "curseforge:<projectId>" or "file:<file name>" (local: nobody else can download it).
 * Links (fashion://pack/<base64url(zlib(json))>) carry a compact form without hashes and URLs; applyManifest looks those
 * up on Modrinth/CurseForge before downloading.
 */
class SharingBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY analyzingChanged)
    /// Outcome of the last applyManifest: { instanceId, added, upToDate, failed: [name], local: [name] }
    Q_PROPERTY(QVariantMap result READ result NOTIFY busyChanged)
    Q_PROPERTY(bool includeClientDefault READ includeClientDefault WRITE setIncludeClientDefault NOTIFY includeClientDefaultChanged)

   public:
    explicit SharingBridge(QObject* parent = nullptr);
    ~SharingBridge() override;

    bool busy() const { return m_task != nullptr || m_stage != Stage::Idle; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    bool analyzing() const { return m_analyzeTask != nullptr; }
    QVariantMap result() const { return m_result; }
    bool includeClientDefault() const;
    void setIncludeClientDefault(bool on);

    /// options { includeClient: bool (default true), exclude: [key] }
    Q_INVOKABLE QVariantMap createManifest(const QString& instanceId, const QVariantMap& options = {});
    /// Looks up files without metadata on Modrinth (by SHA-1) and fetches which side each mod runs on. Emits analyzed().
    Q_INVOKABLE void analyze(const QString& instanceId);
    /// The same filter applyManifest uses: drops excluded keys and, without includeClient, client-only mods and all packs.
    Q_INVOKABLE QVariantMap filterManifest(const QVariantMap& manifest, const QVariantMap& options) const;
    /// [{ instanceId, name, mc, loader, sameVersion, missing: [mod], missingCount, extra: [mod] }], best match first.
    Q_INVOKABLE QVariantList matchInstances(const QVariantMap& manifest);
    /// options { targetInstanceId: "" = new instance | id, includeClient, exclude: [key], name, keepAsIs: bool }
    Q_INVOKABLE bool applyManifest(const QVariantMap& manifest, const QVariantMap& options);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

    Q_INVOKABLE QString packLink(const QVariantMap& manifest) const;
    /// A manifest from a fashion://pack/ link (anywhere in the text) or pasted manifest JSON; { error } when it isn't one.
    Q_INVOKABLE QVariantMap parseLink(const QString& text) const;
    /// Writes a .fashpack; with no path asks where. Returns the path written, "" when cancelled or failed.
    Q_INVOKABLE QString saveFile(const QVariantMap& manifest, const QString& path = {});
    /// Reads a .fashpack (or a text file holding a link); with no path asks which. {} when cancelled, { error } on failure.
    Q_INVOKABLE QVariantMap openFile(const QString& path = {});
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE QString instanceName(const QString& instanceId) const;
    /// All instances as [{ value: id, label: name }] for pickers.
    Q_INVOKABLE QVariantList instanceOptions() const;

   signals:
    void busyChanged();
    void progressChanged();
    void errorChanged();
    void analyzingChanged();
    void includeClientDefaultChanged();
    void analyzed(const QString& instanceId);
    void applied(const QString& instanceId);

   private:
    enum class Stage { Idle, Resolving, Creating, Downloading };

    QVariantList scanFolder(const QString& dir, const QString& kind, bool hashAll);
    QVariantMap scanInstance(const QString& instanceId, bool hashAll, QVariantMap* versionInfo = nullptr);
    QString fileSha1(const QString& path);
    void loadCache();
    void saveCache() const;

    void resolveStep();
    void createStep();
    void downloadStep();
    void finishApply(const QString& error);
    void setError(const QString& e);
    void setStatus(const QString& s, double p = -1);
    void runAutoTest();

    // cache: sha1 -> { projectId, versionId, url, file }, Modrinth project id -> { side, name, slug }
    QVariantMap m_hashCache;
    QVariantMap m_projectCache;
    QHash<QString, QPair<QString, QString>> m_sha1Memo;  // path -> (size:mtime, sha1)

    Task::Ptr m_analyzeTask;
    QString m_analyzeId;

    Stage m_stage = Stage::Idle;
    Task::Ptr m_task;
    QVariantMap m_manifest;  // filtered, being applied
    QVariantMap m_options;
    QString m_targetId;
    QStringList m_failed;
    QVariantMap m_result;
    QString m_status;
    double m_progress = -1;
    QString m_error;
};
