// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include "QObjectPtr.h"

class MainWindow;
class LaunchController;

/**
 * Actions the QML shell can trigger: launching, killing, folders, settings.
 *
 * Features that have no QML screen yet (instance creation, import, export, instance editor)
 * fall back to the classic Prism dialogs. They are driven through a hidden classic MainWindow,
 * so they behave exactly like upstream until a QML replacement exists.
 */
class LauncherBridge : public QObject {
    Q_OBJECT
    /// instanceId -> { status: string, progress: 0..1 or -1, phase: "launching" | "running" }
    Q_PROPERTY(QVariantMap launches READ launches NOTIFY launchesChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    /// [{ key, name }] — every language Prism knows; the new UI itself ships English and Russian
    Q_PROPERTY(QVariantList languages READ languages NOTIFY languagesChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(bool onboardingDone READ onboardingDone NOTIFY onboardingDoneChanged)
    /// { state: "" | "uploading" | "done" | "failed", url, error }
    Q_PROPERTY(QVariantMap logShare READ logShare NOTIFY logShareChanged)

   public:
    explicit LauncherBridge(QObject* parent = nullptr);
    ~LauncherBridge() override;

    QVariantMap launches() const { return m_launches; }
    QString version() const;
    QString appName() const;
    QVariantList languages() const;
    QString language() const;
    bool onboardingDone() const;

    Q_INVOKABLE void setLanguage(const QString& key);
    QVariantMap logShare() const { return m_logShare; }
    /// The log that "Share game log" would upload (newest crash report, else latest.log); "" if none
    Q_INVOKABLE QString latestLogFile(const QString& id) const;
    Q_INVOKABLE QString pasteHost() const;
    Q_INVOKABLE void shareLatestLog(const QString& id);
    Q_INVOKABLE void resetLogShare()
    {
        m_logShare.clear();
        emit logShareChanged();
    }
    Q_INVOKABLE void finishOnboarding();

    Q_INVOKABLE bool launch(const QString& id, bool offline = false);
    Q_INVOKABLE void kill(const QString& id);
    Q_INVOKABLE void openInstanceFolder(const QString& id) const;
    Q_INVOKABLE void openScreenshotsFolder(const QString& id) const;
    Q_INVOKABLE void editInstance(const QString& id, const QString& page = QString());
    Q_INVOKABLE void showConsole(const QString& id);

    Q_INVOKABLE void newInstance();
    Q_INVOKABLE void copyInstance(const QString& id);
    Q_INVOKABLE void deleteInstance(const QString& id);
    Q_INVOKABLE void exportInstance(const QString& id);
    /// { mc, loader ("" | fabric | quilt | forge | neoforge), loaderVersion }
    Q_INVOKABLE QVariantMap versionInfo(const QString& id) const;
    /// Switch Minecraft version and/or mod loader; returns an error message or "" on success
    Q_INVOKABLE QString changeVersion(const QString& id, const QString& mc, const QString& loader, const QString& loaderVersion);
    Q_INVOKABLE void renameInstance(const QString& id, const QString& name);
    Q_INVOKABLE void changeIcon(const QString& id);

    Q_INVOKABLE void openSettings(const QString& page = QString());
    Q_INVOKABLE void openClassicWindow();
    Q_INVOKABLE void openUrl(const QString& url) const;

   signals:
    void launchesChanged();
    void launchFailed(const QString& instanceName, const QString& reason);
    void notify(const QString& message);
    void languagesChanged();
    void languageChanged();
    void onboardingDoneChanged();
    void logShareChanged();

   private:
    void trackController(const QString& id, LaunchController* controller);
    void setLaunch(const QString& id, const QVariantMap& state);
    void clearLaunch(const QString& id);
    MainWindow* classic();
    void runClassicAction(const QString& id, const char* slot);

    QVariantMap m_launches;
    QVariantMap m_logShare;
    shared_qobject_ptr<class NetJob> m_logJob;
    QPointer<MainWindow> m_classic;
};
