// SPDX-License-Identifier: GPL-3.0-only
#include "LauncherBridge.h"

#include <QDir>
#include <QTimer>
#include <QUrl>

#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "InstanceList.h"
#include "LaunchController.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/MainWindow.h"

LauncherBridge::LauncherBridge(QObject* parent) : QObject(parent)
{
    connect(APPLICATION, &Application::launchStarted, this, [this](MinecraftInstance* inst, LaunchController* controller) {
        trackController(inst->id(), controller);
    });
}

LauncherBridge::~LauncherBridge()
{
    delete m_classic;
}

QString LauncherBridge::version() const
{
    return BuildConfig.printableVersionString();
}

QString LauncherBridge::appName() const
{
    return BuildConfig.LAUNCHER_DISPLAYNAME;
}

static MinecraftInstance* findInstance(const QString& id)
{
    return APPLICATION->instances()->getInstanceById(id);
}

bool LauncherBridge::launch(const QString& id, bool offline)
{
    auto* inst = findInstance(id);
    if (!inst) {
        return false;
    }
    return APPLICATION->launch(inst, offline ? LaunchMode::Offline : LaunchMode::Normal);
}

void LauncherBridge::kill(const QString& id)
{
    if (auto* inst = findInstance(id); inst && inst->isRunning()) {
        APPLICATION->kill(inst);
    }
}

void LauncherBridge::trackController(const QString& id, LaunchController* controller)
{
    setLaunch(id, { { "phase", "launching" }, { "status", tr("Getting ready") }, { "progress", -1 } });

    auto* inst = findInstance(id);
    // The launch task appears only after account checks; follow its status and progress.
    if (inst) {
        connect(inst, &BaseInstance::launchTaskChanged, controller, [this, id](LaunchTask* task) {
            if (!task) {
                return;
            }
            connect(task, &Task::status, this, [this, id](const QString& status) {
                auto state = m_launches.value(id).toMap();
                if (state.value("phase") == "launching" && !status.isEmpty()) {
                    state["status"] = status;
                    setLaunch(id, state);
                }
            });
            connect(task, &Task::progress, this, [this, id](qint64 current, qint64 total) {
                auto state = m_launches.value(id).toMap();
                if (state.value("phase") == "launching") {
                    state["progress"] = total > 0 ? double(current) / double(total) : -1.0;
                    setLaunch(id, state);
                }
            });
            // LaunchTask exposes no signal for "the game process is up", only a pid. Poll it cheaply.
            auto* poll = new QTimer(task);
            poll->setInterval(400);
            connect(poll, &QTimer::timeout, this, [this, id, task, poll] {
                if (task->pid() > 0) {
                    poll->stop();
                    setLaunch(id, { { "phase", "running" }, { "status", tr("Playing") }, { "progress", -1 } });
                }
            });
            poll->start();
        });
    }

    connect(controller, &Task::failed, this, [this, id](const QString& reason) {
        auto* inst = findInstance(id);
        emit launchFailed(inst ? inst->name() : id, reason);
    });
    connect(controller, &Task::finished, this, [this, id] { clearLaunch(id); });
}

void LauncherBridge::setLaunch(const QString& id, const QVariantMap& state)
{
    m_launches[id] = state;
    emit launchesChanged();
}

void LauncherBridge::clearLaunch(const QString& id)
{
    if (m_launches.remove(id)) {
        emit launchesChanged();
    }
}

void LauncherBridge::openInstanceFolder(const QString& id) const
{
    if (auto* inst = findInstance(id)) {
        DesktopServices::openPath(QDir(inst->instanceRoot()).absolutePath(), true);
    }
}

void LauncherBridge::openScreenshotsFolder(const QString& id) const
{
    if (auto* inst = findInstance(id)) {
        const QString path = QDir(inst->gameRoot()).filePath("screenshots");
        QDir().mkpath(path);
        DesktopServices::openPath(path, true);
    }
}

void LauncherBridge::editInstance(const QString& id, const QString& page)
{
    if (auto* inst = findInstance(id)) {
        APPLICATION->showInstanceWindow(inst, page);
    }
}

void LauncherBridge::showConsole(const QString& id)
{
    editInstance(id, "console");
}

MainWindow* LauncherBridge::classic()
{
    if (!m_classic) {
        // Never shown: it only hosts the classic dialogs and the logic attached to them.
        m_classic = new MainWindow();
    }
    return m_classic;
}

void LauncherBridge::runClassicAction(const QString& id, const char* slot)
{
    auto* window = classic();
    if (!id.isEmpty()) {
        QMetaObject::invokeMethod(window, "instanceSelectRequest", Q_ARG(QString, id));
    }
    QMetaObject::invokeMethod(window, slot);
}

void LauncherBridge::newInstance()
{
    runClassicAction({}, "on_actionAddInstance_triggered");
}

void LauncherBridge::copyInstance(const QString& id)
{
    runClassicAction(id, "on_actionCopyInstance_triggered");
}

void LauncherBridge::deleteInstance(const QString& id)
{
    runClassicAction(id, "on_actionDeleteInstance_triggered");
}

void LauncherBridge::exportInstance(const QString& id)
{
    runClassicAction(id, "on_actionExportInstanceZip_triggered");
}

void LauncherBridge::changeIcon(const QString& id)
{
    runClassicAction(id, "on_actionChangeInstIcon_triggered");
}

void LauncherBridge::renameInstance(const QString& id, const QString& name)
{
    auto* inst = findInstance(id);
    if (inst && !name.trimmed().isEmpty()) {
        inst->setName(name.trimmed());
    }
}

void LauncherBridge::openSettings(const QString& page)
{
    APPLICATION->ShowGlobalSettings(nullptr, page);
}

void LauncherBridge::openClassicWindow()
{
    APPLICATION->showClassicWindow();
}

void LauncherBridge::openUrl(const QString& url) const
{
    DesktopServices::openUrl(QUrl(url));
}
