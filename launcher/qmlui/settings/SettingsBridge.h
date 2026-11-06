// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariant>

#include "QObjectPtr.h"

class SettingsObject;
class JavaChecker;

/**
 * Launcher-wide and per-instance settings (Java, memory, behaviour, language).
 * Exposed to QML as "Settings".
 *
 * Values are read and written through Prism's own SettingsObjects with the same keys the
 * classic pages use, so both interfaces always agree. Every change applies immediately.
 *
 * QML reads values inside bindings that also touch `revision`, which bumps whenever any
 * watched setting changes, e.g. `checked: { Settings.revision; return Settings.get("ShowConsole") }`.
 *
 * Per-instance settings are grouped the way the classic instance page groups them. A group is
 * "overridden" when its gate setting (OverrideMemory, OverrideWindow, ...) is on; otherwise the
 * instance follows the global value.
 */
class SettingsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    /// Physical memory of this machine in MiB (upper bound for the memory sliders)
    Q_PROPERTY(int totalRamMiB READ totalRamMiB CONSTANT)
    /// [{ path, version, major, arch, is64 }] — Java installs found on this machine
    Q_PROPERTY(QVariantList javaInstalls READ javaInstalls NOTIFY javaInstallsChanged)
    Q_PROPERTY(bool javaScanning READ javaScanning NOTIFY javaInstallsChanged)
    Q_PROPERTY(bool javaTesting READ javaTesting NOTIFY javaTestingChanged)
    Q_PROPERTY(bool javaDownloaderEnabled READ javaDownloaderEnabled CONSTANT)

   public:
    explicit SettingsBridge(QObject* parent = nullptr);
    ~SettingsBridge() override;

    int revision() const { return m_revision; }
    int totalRamMiB() const;
    QVariantList javaInstalls() const { return m_javaInstalls; }
    bool javaScanning() const { return m_javaScanning; }
    bool javaTesting() const { return m_javaTesting; }
    bool javaDownloaderEnabled() const;

    // Launcher-wide settings
    Q_INVOKABLE QVariant get(const QString& key) const;
    Q_INVOKABLE void set(const QString& key, const QVariant& value);
    Q_INVOKABLE void reset(const QString& key);
    Q_INVOKABLE QVariant defaultValue(const QString& key) const;

    // Per-instance settings. get() returns the effective value (the global one when not overridden).
    Q_INVOKABLE QVariant instGet(const QString& instanceId, const QString& key);
    Q_INVOKABLE void instSet(const QString& instanceId, const QString& key, const QVariant& value);
    Q_INVOKABLE void instReset(const QString& instanceId, const QString& key);
    /// group: "java" | "memory" | "window" | "behaviour"
    Q_INVOKABLE bool instOverride(const QString& instanceId, const QString& group);
    Q_INVOKABLE void setInstOverride(const QString& instanceId, const QString& group, bool on);

    /// [{ profileId, name, kind }]
    Q_INVOKABLE QVariantList accounts() const;
    /// Suggested maximum memory in MiB: depends on this computer's RAM and, for an instance, its number of mods
    Q_INVOKABLE int recommendedMaxMemory(const QString& instanceId = QString()) const;

    // Java
    Q_INVOKABLE void scanJava();
    /// Turns a file URL from a file dialog into a normalized path; empty if it is not an executable file
    Q_INVOKABLE QString javaPathFromUrl(const QUrl& url) const;
    /// Plain-language problem with these JVM arguments, or an empty string
    Q_INVOKABLE QString jvmArgsProblem(const QString& args) const;
    /// Runs the Java checker; ends with javaTestFinished()
    Q_INVOKABLE void testJava(const QString& path, const QString& jvmArgs, int minMem, int maxMem, int permGen);
    /// Ask the QML shell to show its Java download dialog. instanceId may be empty.
    Q_INVOKABLE void downloadJava(const QString& instanceId = QString());
    /// Look for Java installs again (e.g. after a download)
    Q_INVOKABLE void rescanJava();

    // Folders
    Q_INVOKABLE QString folderFromUrl(const QUrl& url) const;
    /// Plain-language warning about using this folder for instances, or an empty string
    Q_INVOKABLE QString instanceDirProblem(const QString& path) const;
    Q_INVOKABLE void openFolder(const QString& path) const;

   signals:
    /// Opens the QML Java download dialog
    void javaDownloadRequested(const QString& instanceId);
    void revisionChanged();
    void javaInstallsChanged();
    void javaTestingChanged();
    void javaTestFinished(bool ok, const QString& title, const QString& details);

   private:
    SettingsObject* instanceSettings(const QString& instanceId);
    void watch(SettingsObject* settings);
    void bump();
    void rebuildJavaList();
    void finishJavaTest(bool ok, const QString& title, const QString& details);

    int m_revision = 0;
    bool m_bumpQueued = false;
    QHash<QString, QPointer<SettingsObject>> m_watched;

    QVariantList m_javaInstalls;
    bool m_javaScanning = false;
    bool m_javaTesting = false;
    shared_qobject_ptr<JavaChecker> m_checker;
};
