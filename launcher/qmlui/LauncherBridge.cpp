// SPDX-License-Identifier: GPL-3.0-only
#include "LauncherBridge.h"

#include <QDir>
#include <QHash>
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
#include "ui/MainWindow.h"
#include "ui/GuiUtil.h"
#include "FileSystem.h"
#include "net/NetJob.h"
#include "net/PasteUpload.h"
#include <QFileInfo>

LauncherBridge::LauncherBridge(QObject* parent) : QObject(parent)
{
    auto* tm = APPLICATION->translations();
    // The index arrives asynchronously (first run picks the system language then): re-check once it's here.
    // Queued: indexReceived() stores the system language only after it has reloaded the rows.
    connect(tm, &QAbstractItemModel::rowsInserted, this, &LauncherBridge::enforceSupportedLanguage, Qt::QueuedConnection);
    enforceSupportedLanguage();
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

QString LauncherBridge::logo() const
{
    return "qrc:/" + BuildConfig.LAUNCHER_SVGFILENAME;
}

QString LauncherBridge::appName() const
{
    return BuildConfig.LAUNCHER_DISPLAYNAME;
}

namespace {
struct UiLanguage {
    const char* key;   // Prism's translation key (what the "Language" setting stores)
    const char* code;  // suffix of qmlui_<code>.qm
    const char* name;  // native name
};
// The new UI is fully translated into exactly these. Order = order in the pickers.
const UiLanguage g_uiLanguages[] = {
    { "en_US", "", "English" },          { "ru", "ru", "Русский" },        { "es", "es", "Español" },
    { "pt_BR", "pt_BR", "Português (Brasil)" }, { "de", "de", "Deutsch" }, { "fr", "fr", "Français" },
    { "it", "it", "Italiano" },          { "pl", "pl", "Polski" },         { "uk", "uk", "Українська" },
    { "tr", "tr", "Türkçe" },            { "zh", "zh_CN", "简体中文" },     { "ja", "ja", "日本語" },
    { "ko", "ko", "한국어" },            { "cs", "cs", "Čeština" },        { "id", "id", "Bahasa Indonesia" },
};
}  // namespace

QString LauncherBridge::supportedLanguage(const QString& key)
{
    static const QHash<QString, QString> aliases = {
        { "pt", "pt_BR" }, { "pt_PT", "pt_BR" }, { "zh_CN", "zh" },    { "zh_TW", "zh" }, { "zh_Hant_HK", "zh" },
        { "zh_HK", "zh" }, { "lzh", "zh" },      { "nan", "zh" },      { "en", "en_US" }, { "be", "ru" },
        { "rue", "uk" },   { "sk", "cs" },       { "ms", "id" },       { "szl", "pl" },   { "ca@valencia", "es" },
    };
    if (aliases.contains(key)) {
        return aliases.value(key);
    }
    for (const auto& l : g_uiLanguages) {
        if (key == l.key) {
            return key;
        }
    }
    // Regional variants (es_AR, de_CH, fr_CA, en_GB...) fall back to their base language
    const QString base = key.section('_', 0, 0).section('@', 0, 0);
    for (const auto& l : g_uiLanguages) {
        if (base == QString(l.key).section('_', 0, 0)) {
            return l.key;
        }
    }
    return "en_US";
}

QString LauncherBridge::uiTranslationCode(const QString& key)
{
    const QString supported = supportedLanguage(key);
    for (const auto& l : g_uiLanguages) {
        if (supported == l.key) {
            return l.code;
        }
    }
    return {};
}

QVariantList LauncherBridge::languages() const
{
    QVariantList list;
    for (const auto& l : g_uiLanguages) {
        list.append(QVariantMap{ { "key", l.key }, { "name", QString::fromUtf8(l.name) } });
    }
    return list;
}

QString LauncherBridge::language() const
{
    return supportedLanguage(APPLICATION->settings()->get("Language").toString());
}

void LauncherBridge::enforceSupportedLanguage()
{
    const QString saved = APPLICATION->settings()->get("Language").toString();
    if (saved.isEmpty()) {
        return;  // not chosen yet; Prism picks the system language once its index arrives, then we come back here
    }
    const QString key = supportedLanguage(saved);
    auto* tm = APPLICATION->translations();
    if (key != saved) {
        APPLICATION->settings()->set("Language", key);
    }
    if (tm->selectedLanguage() != key) {
        // Also covers a language picked before the index arrived (selectLanguage then fell back to English)
        tm->selectLanguage(key);
        tm->updateLanguage(key);
    }
    if (key != saved) {
        emit languageChanged();
    }
}

void LauncherBridge::setLanguage(const QString& key)
{
    if (key.isEmpty() || key == language()) {
        return;
    }
    const QString supported = supportedLanguage(key);
    APPLICATION->settings()->set("Language", supported);
    auto* tm = APPLICATION->translations();
    tm->selectLanguage(supported);
    tm->updateLanguage(supported);  // downloads Prism's core translation if it isn't on disk yet
    emit languageChanged();         // QmlShell swaps its own translator and retranslates the QML
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
