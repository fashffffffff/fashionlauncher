// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariant>

class BaseInstance;

/**
 * Pre-launch checks, mod changelogs before updates, import from other launchers, recommended settings inside modpacks.
 * Exposed to QML as "Tools".
 *
 * - Pre-launch checks: LauncherBridge::launch asks holdLaunch() first; when something is certainly wrong the launch
 *   is paused and launchBlocked() carries the problems to ToolsLaunchCheck.qml ("Play anyway" / "Fix").
 * - Other launchers: refreshLaunchers()/scanFolder() fill `launchers`; CreateBridge::createFromLaunchers imports them.
 * - Modpack settings: fashion-launcher.json inside .mrpack/.zip (docs/MODPACK_SETTINGS.md), applied on import by
 *   CreateBridge through applyPackSettings(); writePackSettings() puts it into an export.
 */
class ToolsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool checksEnabled READ checksEnabled WRITE setChecksEnabled NOTIFY checksEnabledChanged)
    Q_PROPERTY(QVariantList launchers READ launchers NOTIFY launchersChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY launchersChanged)
    Q_PROPERTY(QString scannedFolder READ scannedFolder NOTIFY launchersChanged)
    /// FFPRISM_TOOLS automation for snapshots: check:<id> | launchers | folder:<path>
    Q_PROPERTY(QString automation READ automation CONSTANT)

   public:
    explicit ToolsBridge(QObject* parent = nullptr);
    ~ToolsBridge() override;
    static ToolsBridge* instance() { return s_instance; }

    bool checksEnabled() const;
    void setChecksEnabled(bool on);
    QVariantList launchers() const { return m_launchers; }
    bool scanning() const { return m_scanning; }
    QString scannedFolder() const { return m_scannedFolder; }
    QString automation() const { return qEnvironmentVariable("FFPRISM_TOOLS"); }

    /// Called right before a launch. Returns true when the launch must wait for the user (launchBlocked was emitted).
    bool holdLaunch(const QString& id, bool offline);

    /// The problems of an instance right now (same as the pre-launch check).
    Q_INVOKABLE QVariantList check(const QString& id) const;
    /// Let the next launch of this instance skip the checks ("Play anyway").
    Q_INVOKABLE void allowOnce(const QString& id);
    /// Does every fix that needs no choice (turning files off). Returns how many files were turned off.
    Q_INVOKABLE int autoFix(const QVariantList& problems);
    /// Turns mod files off the way the launcher does (adds ".disabled"). Returns how many succeeded.
    Q_INVOKABLE int disableFiles(const QStringList& paths);

    /// Opens "Find mods" for an instance with a search filled in: the mods tab picks this up when it opens.
    Q_INVOKABLE void requestFind(const QString& id, const QString& query);
    Q_INVOKABLE bool hasPendingFind(const QString& id) const { return m_pendingFind.contains(id); }
    Q_INVOKABLE QString takePendingFind(const QString& id) { return m_pendingFind.take(id); }

    /// Looks for other launchers at their default places (async; watch `scanning`).
    Q_INVOKABLE void refreshLaunchers();
    /// Looks for instances in a folder the user picked (async).
    Q_INVOKABLE void scanFolder(const QString& pathOrUrl);
    Q_INVOKABLE QVariantMap launcherEntry(const QString& key) const;

    /// The instance's memory/JVM settings as a fashion-launcher.json object (empty when it uses the defaults).
    Q_INVOKABLE QVariantMap packSettings(const QString& id) const;
    /// Writes fashion-launcher.json into an exported archive, or into the instance folder when archivePath is empty
    /// (an instance export then carries it at its root). Returns an error, or "".
    Q_INVOKABLE QString writePackSettings(const QString& id, const QString& archivePath = QString()) const;
    /// Reads fashion-launcher.json from a modpack archive and applies it to a new instance; tells the user what changed.
    static QString applyPackSettings(BaseInstance* instance, const QString& archivePath);

    /// Shows a message to the user (as a toast) a moment from now.
    static void announce(const QString& message);

   signals:
    void checksEnabledChanged();
    void launchersChanged();
    void launchBlocked(const QString& instanceId, const QString& name, bool offline, const QVariantList& problems);
    void notice(const QString& message);

   private:
    void runScan(std::function<QVariantList()> scan, const QString& folder);

    static ToolsBridge* s_instance;
    QSet<QString> m_allowOnce;
    QHash<QString, QString> m_pendingFind;
    QVariantList m_launchers;
    bool m_scanning = false;
    QString m_scannedFolder;
    int m_scanGeneration = 0;
};
