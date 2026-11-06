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
#include "minecraft/PackProfile.h"
#include "minecraft/Component.h"
#include "net/Mode.h"
#include "settings/SettingsObject.h"
#include "translations/TranslationsModel.h"
#include "ui/GuiUtil.h"
#include "FileSystem.h"
#include "net/NetJob.h"
#include "net/PasteUpload.h"
#include <QFileInfo>

LauncherBridge::LauncherBridge(QObject* parent) : QObject(parent)
{
    auto* tm = APPLICATION->translations();
    connect(tm, &QAbstractItemModel::modelReset, this, &LauncherBridge::languagesChanged);
    connect(tm, &QAbstractItemModel::rowsInserted, this, &LauncherBridge::languagesChanged);
    connect(APPLICATION, &Application::launchStarted, this, [this](MinecraftInstance* inst, LaunchController* controller) {
        trackController(inst->id(), controller);
    });
}

LauncherBridge::~LauncherBridge() = default;

QString LauncherBridge::version() const
{
    return BuildConfig.printableVersionString();
}

QString LauncherBridge::logo() const
{
    return "qrc:/" + BuildConfig.LAUNCHER_SVGFILENAME;
}

QString LauncherBridge::appName() const
{
    return BuildConfig.LAUNCHER_DISPLAYNAME;
}

QVariantList LauncherBridge::languages() const
{
    QVariantList list;
    auto* tm = APPLICATION->translations();
    for (int i = 0; i < tm->rowCount(); i++) {
        auto idx = tm->index(i, 0);
        QString name = idx.data(Qt::DisplayRole).toString();
        if (!name.isEmpty()) {
            name[0] = name[0].toUpper();  // Qt gives "русский", people expect "Русский"
        }
        list.append(QVariantMap{ { "key", idx.data(Qt::UserRole).toString() }, { "name", name } });
    }
    return list;
}

QString LauncherBridge::language() const
{
    return APPLICATION->settings()->get("Language").toString();
}

void LauncherBridge::setLanguage(const QString& key)
{
    if (key.isEmpty() || key == language()) {
        return;
    }
    APPLICATION->settings()->set("Language", key);
    APPLICATION->translations()->selectLanguage(key);
    emit languageChanged();  // QmlShell swaps its own translator and retranslates the QML
}

bool LauncherBridge::onboardingDone() const
{
    return APPLICATION->settings()->get("QmlOnboardingDone").toBool();
}

void LauncherBridge::finishOnboarding()
{
    APPLICATION->settings()->set("QmlOnboardingDone", true);
    emit onboardingDoneChanged();
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
                    // The game window is up: free the launcher's memory until it closes.
                    auto* settings = APPLICATION->settings();
                    if (settings->get("QmlUnloadWhilePlaying").toBool() || settings->get("CloseAfterLaunch").toBool()) {
                        QTimer::singleShot(1500, APPLICATION, [] { APPLICATION->unloadShellForGame(); });
                    }
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
    if (!findInstance(id)) {
        return;
    }
    // Classic page names map onto the instance page's tabs.
    static const QHash<QString, QString> tabs = {
        { "console", "log" }, { "log", "log" }, { "settings", "settings" }, { "worlds", "worlds" },
        { "mods", "mods" }, { "resourcepacks", "resourcepacks" }, { "shaderpacks", "shaderpacks" }, { "screenshots", "screenshots" },
    };
    emit instancePageRequested(id, tabs.value(page, "overview"));
}

void LauncherBridge::showConsole(const QString& id)
{
    editInstance(id, "log");
}

void LauncherBridge::newInstance()
{
    emit createRequested();
}

void LauncherBridge::copyInstance(const QString& id)
{
    emit dialogRequested("copy", id);
}

void LauncherBridge::deleteInstance(const QString& id)
{
    emit dialogRequested("delete", id);
}

void LauncherBridge::exportInstance(const QString& id)
{
    emit dialogRequested("export", id);
}

void LauncherBridge::changeIcon(const QString& id)
{
    emit dialogRequested("icon", id);
}

void LauncherBridge::trackStep(const QString& id, Task* task)
{
    if (!task) {
        return;
    }
    auto update = [this, id](const QString& status, double progress) {
        auto state = m_launches.value(id).toMap();
        if (state.value("phase") != "launching") {
            return;
        }
        if (!status.isEmpty()) {
            state["status"] = status;
        }
        if (progress >= -1) {
            state["progress"] = progress;
        }
        setLaunch(id, state);
    };
    update(task->getStatus(), -1);
    connect(task, &Task::status, this, [update](const QString& status) { update(status, -2); });
    connect(task, &Task::progress, this,
            [update](qint64 current, qint64 total) { update({}, total > 0 ? double(current) / double(total) : -1.0); });
}

namespace {
const QList<QPair<QString, QString>> kLoaders = {
    { "fabric", "net.fabricmc.fabric-loader" },
    { "quilt", "org.quiltmc.quilt-loader" },
    { "forge", "net.minecraftforge" },
    { "neoforge", "net.neoforged" },
};
}  // namespace

QVariantMap LauncherBridge::versionInfo(const QString& id) const
{
    auto* inst = findInstance(id);
    if (!inst) {
        return {};
    }
    auto* profile = inst->getPackProfile();
    QVariantMap info{ { "mc", profile->getComponentVersion("net.minecraft") }, { "loader", "" }, { "loaderVersion", "" } };
    for (const auto& [key, uid] : kLoaders) {
        if (const auto v = profile->getComponentVersion(uid); !v.isEmpty()) {
            info["loader"] = key;
            info["loaderVersion"] = v;
            break;
        }
    }
    return info;
}

QString LauncherBridge::changeVersion(const QString& id, const QString& mc, const QString& loader, const QString& loaderVersion)
{
    auto* inst = findInstance(id);
    if (!inst) {
        return tr("This instance no longer exists.");
    }
    if (inst->isRunning()) {
        return tr("Close the game before changing its version.");
    }
    if (mc.isEmpty()) {
        return tr("Pick a Minecraft version.");
    }
    QString wantedUid;
    for (const auto& [key, uid] : kLoaders) {
        if (key == loader) {
            wantedUid = uid;
        }
    }
    if (!loader.isEmpty() && (wantedUid.isEmpty() || loaderVersion.isEmpty())) {
        return tr("Pick a mod loader version.");
    }

    auto* profile = inst->getPackProfile();
    if (profile->getComponentVersion("net.minecraft") != mc) {
        profile->setComponentVersion("net.minecraft", mc, true);
    }
    // Only one mod loader at a time; their mappings go with them.
    for (const auto& [key, uid] : kLoaders) {
        if (uid != wantedUid && profile->getComponent(uid)) {
            profile->remove(uid);
        }
    }
    if (loader != "fabric" && profile->getComponent("net.fabricmc.intermediary")) {
        profile->remove("net.fabricmc.intermediary");
    }
    if (loader != "quilt" && profile->getComponent("org.quiltmc.hashed")) {
        profile->remove("org.quiltmc.hashed");
    }
    if (!wantedUid.isEmpty()) {
        profile->setComponentVersion(wantedUid, loaderVersion);
        if (auto component = profile->getComponent(wantedUid)) {
            component->setEnabled(true);
        }
    }
    profile->resolve(Net::Mode::Online);
    profile->saveNow();
    return {};
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
    emit settingsRequested(page);
}

void LauncherBridge::openUrl(const QString& url) const
{
    DesktopServices::openUrl(QUrl(url));
}

QString LauncherBridge::latestLogFile(const QString& id) const
{
    auto* inst = findInstance(id);
    if (!inst) {
        return {};
    }
    QFileInfo best;
    const auto crashes = QDir(FS::PathCombine(inst->gameRoot(), "crash-reports")).entryInfoList({ "*.txt" }, QDir::Files, QDir::Time);
    if (!crashes.isEmpty()) {
        best = crashes.first();
    }
    const QFileInfo latest(FS::PathCombine(inst->gameRoot(), "logs/latest.log"));
    if (latest.exists() && (!best.exists() || latest.lastModified() > best.lastModified())) {
        best = latest;
    }
    return best.exists() ? best.absoluteFilePath() : QString();
}

QString LauncherBridge::pasteHost() const
{
    auto type = static_cast<PasteUpload::Type>(APPLICATION->settings()->get("PastebinType").toInt());
    QString base = APPLICATION->settings()->get("PastebinCustomAPIBase").toString();
    return QUrl(base.isEmpty() ? type.defaultBase() : base).host();
}

void LauncherBridge::shareLatestLog(const QString& id)
{
    const QString file = latestLogFile(id);
    auto fail = [this](const QString& reason) {
        m_logShare = { { "state", "failed" }, { "error", reason } };
        emit logShareChanged();
    };
    if (file.isEmpty()) {
        fail(tr("There is no game log yet. Play once first."));
        return;
    }
    auto data = FS::read(file);
    if (!data) {
        fail(tr("Couldn't read the log."));
        return;
    }
    QString text = *data;
    auto type = static_cast<PasteUpload::Type>(APPLICATION->settings()->get("PastebinType").toInt());
    QString base = APPLICATION->settings()->get("PastebinCustomAPIBase").toString();
    if (base.isEmpty()) {
        base = type.defaultBase();
    }
    // mclo.gs keeps at most 25000 lines: keep the start and the end, where crashes are.
    if (type == PasteUpload::Type::Mclogs) {
        const QStringList lines = text.split('\n');
        if (lines.size() > 25000) {
            text = lines.mid(0, 10000).join('\n') + "\n\n----- Log truncated -----\n\n" + lines.mid(lines.size() - 14900).join('\n');
        }
    }
    m_logShare = { { "state", "uploading" } };
    emit logShareChanged();

    auto job = NetJob::Ptr(new NetJob("Log Upload", APPLICATION->network()));
    auto [pasteJob, pasteLink] = type.make(std::move(text), base);
    job->addNetAction(pasteJob);
    connect(job.get(), &Task::failed, this, [fail](const QString& reason) { fail(reason); });
    connect(job.get(), &Task::succeeded, this, [this, link = pasteLink, fail] {
        if (link->isEmpty()) {
            fail(tr("The upload link is empty."));
            return;
        }
        GuiUtil::setClipboardText(*link);
        m_logShare = { { "state", "done" }, { "url", *link } };
        emit logShareChanged();
    });
    m_logJob = job;  // kept until the next upload
    job->start();
}
