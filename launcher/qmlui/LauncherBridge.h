// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

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

   public:
    explicit LauncherBridge(QObject* parent = nullptr);
    ~LauncherBridge() override;

    QVariantMap launches() const { return m_launches; }
    QString version() const;
    QString appName() const;

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
    Q_INVOKABLE void renameInstance(const QString& id, const QString& name);
    Q_INVOKABLE void changeIcon(const QString& id);

    Q_INVOKABLE void openSettings(const QString& page = QString());
    Q_INVOKABLE void openClassicWindow();
    Q_INVOKABLE void openUrl(const QString& url) const;

   signals:
    void launchesChanged();
    void launchFailed(const QString& instanceName, const QString& reason);
    void notify(const QString& message);

   private:
    void trackController(const QString& id, LaunchController* controller);
    void setLaunch(const QString& id, const QVariantMap& state);
    void clearLaunch(const QString& id);
    MainWindow* classic();
    void runClassicAction(const QString& id, const char* slot);

    QVariantMap m_launches;
    QPointer<MainWindow> m_classic;
};
