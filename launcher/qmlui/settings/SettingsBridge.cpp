// SPDX-License-Identifier: GPL-3.0-only
#include "SettingsBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "HardwareInfo.h"
#include "InstanceList.h"
#include "java/JavaChecker.h"
#include "java/JavaInstall.h"
#include "java/JavaInstallList.h"
#include "java/JavaUtils.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/auth/AccountList.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "ui/java/InstallJavaDialog.h"

namespace {
struct Group {
    QStringList gates;  // the first gate decides whether the group counts as overridden
    QStringList keys;
};

// Same gates as the classic instance settings page (JavaSettingsWidget / MinecraftSettingsWidget). Classic ties
// the window size and the close/quit options to one checkbox; here close/quit sit with the console options instead.
const QHash<QString, Group>& groups()
{
    static const QHash<QString, Group> s_groups{
        { "java", { { "OverrideJavaLocation", "OverrideJavaArgs" }, { "JavaPath", "IgnoreJavaCompatibility", "JvmArgs" } } },
        { "memory", { { "OverrideMemory" }, { "MinMemAlloc", "MaxMemAlloc", "PermGen", "LowMemWarning" } } },
        { "window", { { "OverrideWindow" }, { "LaunchMaximized", "MinecraftWinWidth", "MinecraftWinHeight" } } },
        // What the launcher does around a game session: launcher window + game log window.
        { "behaviour",
          { { "OverrideMiscellaneous", "OverrideConsole" },
            { "CloseAfterLaunch", "QuitAfterGameStop", "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput" } } },
    };
    return s_groups;
}
// Values loaded from the settings file come back as strings ("true", "4096"). Give QML the
// type of the setting's default instead, so `=== true` and arithmetic work.
QVariant typed(const std::shared_ptr<Setting>& setting, const QVariant& value)
{
    if (!setting || !value.isValid())
        return value;
    QVariant def = setting->defValue();
    if (auto global = APPLICATION->settings()->getSetting(setting->id()); global && global != setting)
        def = global->defValue();
    switch (def.typeId()) {
        case QMetaType::Bool:
            return value.toBool();
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
            return value.toInt();
        default:
            return value;
    }
}
}  // namespace

SettingsBridge::SettingsBridge(QObject* parent) : QObject(parent)
{
    watch(APPLICATION->settings());

    auto* javaList = APPLICATION->javalist();
    connect(javaList, &QAbstractItemModel::modelReset, this, &SettingsBridge::rebuildJavaList);
    if (javaList->isLoaded())
        rebuildJavaList();
}

SettingsBridge::~SettingsBridge() = default;

int SettingsBridge::totalRamMiB() const
{
    return static_cast<int>(HardwareInfo::totalRamMiB());
}

bool SettingsBridge::javaDownloaderEnabled() const
{
    return BuildConfig.JAVA_DOWNLOADER_ENABLED;
}

void SettingsBridge::watch(SettingsObject* settings)
{
    if (!settings)
        return;
    connect(settings, &SettingsObject::SettingChanged, this, &SettingsBridge::bump, Qt::UniqueConnection);
    connect(settings, &SettingsObject::settingReset, this, &SettingsBridge::bump, Qt::UniqueConnection);
}

// Several settings often change in one go (a group override copies all its values); coalesce them.
void SettingsBridge::bump()
{
    if (m_bumpQueued)
        return;
    m_bumpQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_bumpQueued = false;
        m_revision++;
        emit revisionChanged();
    });
}

// ---- launcher-wide ----

QVariant SettingsBridge::get(const QString& key) const
{
    auto* s = APPLICATION->settings();
    return typed(s->getSetting(key), s->get(key));
}

void SettingsBridge::set(const QString& key, const QVariant& value)
{
    auto* s = APPLICATION->settings();
    if (!s->contains(key)) {
        qWarning() << "SettingsBridge: unknown setting" << key;
        return;
    }
    if (typed(s->getSetting(key), s->get(key)) == value)
        return;
    s->set(key, value);

    if (key.startsWith("Proxy")) {
        APPLICATION->updateProxySettings(s->get("ProxyType").toString(), s->get("ProxyAddr").toString(), s->get("ProxyPort").toInt(),
                                         s->get("ProxyUser").toString(), s->get("ProxyPass").toString());
    }
}

void SettingsBridge::reset(const QString& key)
{
    APPLICATION->settings()->reset(key);
}

QVariant SettingsBridge::defaultValue(const QString& key) const
{
    auto setting = APPLICATION->settings()->getSetting(key);
    return setting ? typed(setting, setting->defValue()) : QVariant();
}

// ---- per instance ----

SettingsObject* SettingsBridge::instanceSettings(const QString& instanceId)
{
    if (instanceId.isEmpty())
        return nullptr;
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst)
        return nullptr;
    auto* settings = inst->settings();
    auto& watched = m_watched[instanceId];
    if (watched != settings) {
        watched = settings;
        watch(settings);
    }
    return settings;
}

QVariant SettingsBridge::instGet(const QString& instanceId, const QString& key)
{
    auto* s = instanceSettings(instanceId);
    return s ? typed(s->getSetting(key), s->get(key)) : QVariant();
}

void SettingsBridge::instSet(const QString& instanceId, const QString& key, const QVariant& value)
{
    auto* s = instanceSettings(instanceId);
    if (!s || !s->contains(key))
        return;
    if (typed(s->getSetting(key), s->get(key)) == value)
        return;
    // Picking a Java by hand turns off automatic Java selection for this instance (same as classic).
    if (key == "JavaPath")
        s->set("AutomaticJava", false);
    s->set(key, value);
}

void SettingsBridge::instReset(const QString& instanceId, const QString& key)
{
    if (auto* s = instanceSettings(instanceId))
        s->reset(key);
}

bool SettingsBridge::instOverride(const QString& instanceId, const QString& group)
{
    auto* s = instanceSettings(instanceId);
    if (!s || !groups().contains(group))
        return false;
    for (const auto& gate : groups()[group].gates) {
        if (s->get(gate).toBool())
            return true;
    }
    return false;
}

void SettingsBridge::setInstOverride(const QString& instanceId, const QString& group, bool on)
{
    auto* s = instanceSettings(instanceId);
    if (!s || !groups().contains(group))
        return;
    const auto& g = groups()[group];
    if (on) {
        // Start from what the instance uses right now (the global values), so turning the
        // override on changes nothing until the user edits something.
        QVariantMap current;
        for (const auto& key : g.keys)
            current.insert(key, s->get(key));
        for (const auto& gate : g.gates)
            s->set(gate, true);
        for (auto it = current.cbegin(); it != current.cend(); ++it)
            s->set(it.key(), it.value());
    } else {
        for (const auto& gate : g.gates)
            s->set(gate, false);
        for (const auto& key : g.keys)
            s->reset(key);
    }
}

QVariantList SettingsBridge::accounts() const
{
    QVariantList out;
    auto* list = APPLICATION->accounts();
    for (int i = 0; i < list->count(); i++) {
        const auto account = list->at(i);
        out.append(QVariantMap{ { "profileId", account->profileId() }, { "name", account->profileName() }, { "kind", account->typeString() } });
    }
    return out;
}

// ---- Java ----

void SettingsBridge::scanJava()
{
    if (m_javaScanning)
        return;
    m_javaScanning = true;
    emit javaInstallsChanged();
    auto task = APPLICATION->javalist()->getLoadTask();
    if (!task) {
        rebuildJavaList();
        return;
    }
    connect(task.get(), &Task::finished, this, &SettingsBridge::rebuildJavaList);
}

void SettingsBridge::rebuildJavaList()
{
    auto* list = APPLICATION->javalist();
    QVariantList out;
    for (int i = 0; i < list->count(); i++) {
        auto java = std::dynamic_pointer_cast<JavaInstall>(list->at(i));
        if (!java)
            continue;
        out.append(QVariantMap{ { "path", java->path },
                                { "version", java->id.toString() },
                                { "major", java->id.major() },
                                { "arch", java->arch },
                                { "is64", java->is_64bit } });
    }
    m_javaInstalls = out;
    m_javaScanning = false;
    emit javaInstallsChanged();
}

QString SettingsBridge::javaPathFromUrl(const QUrl& url) const
{
    const QString raw = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (raw.isEmpty())
        return {};
    const QString cooked = FS::NormalizePath(raw);
    QFileInfo info(cooked);
    if (!info.exists() || !info.isExecutable())
        return {};
    return cooked;
}

QString SettingsBridge::jvmArgsProblem(const QString& args) const
{
    static const QRegularExpression s_memRegex("-Xm[sx]");
    static const QRegularExpression s_versionRegex("-version:.*");
    if (args.contains("-XX:PermSize=") || args.contains(s_memRegex) || args.contains("-XX-MaxHeapSize") ||
        args.contains("-XX:InitialHeapSize")) {
        return tr("Set memory with the sliders above instead of -Xmx, -Xms or heap size arguments. "
                  "Remove them from this field.");
    }
    if (args.contains(s_versionRegex))
        return tr("Passing a required Java version (-version:…) is not allowed. Remove it from this field.");
    return {};
}

void SettingsBridge::finishJavaTest(bool ok, const QString& title, const QString& details)
{
    m_checker.reset();
    m_javaTesting = false;
    emit javaTestingChanged();
    emit javaTestFinished(ok, title, details);
}

void SettingsBridge::testJava(const QString& path, const QString& jvmArgs, int minMem, int maxMem, int permGen)
{
    if (m_javaTesting)
        return;
    const QString args = QString(jvmArgs).replace('\n', ' ');
    if (const auto problem = jvmArgsProblem(args); !problem.isEmpty()) {
        emit javaTestFinished(false, tr("Check your Java arguments"), problem);
        return;
    }
    if (JavaUtils::getJavaCheckPath().isEmpty()) {
        emit javaTestFinished(false, tr("Can't test Java"), tr("The Java checker is missing from this installation."));
        return;
    }
    m_javaTesting = true;
    emit javaTestingChanged();

    // Same two steps as the classic test: does the binary run at all, then does it run with these options.
    m_checker.reset(new JavaChecker(path, "", 0, 0, 0, 0));
    connect(m_checker.get(), &JavaChecker::checkFinished, this, [this, path, args, minMem, maxMem, permGen](const JavaChecker::Result& r) {
        if (r.validity != JavaChecker::Result::Validity::Valid) {
            finishJavaTest(false, tr("This Java doesn't work"),
                           tr("It could not be started. Pick another one from the list or choose the java executable yourself."));
            return;
        }
        const int perm = r.javaVersion.requiresPermGen() ? permGen : 0;
        Q_UNUSED(minMem)
        m_checker.reset(new JavaChecker(path, args, maxMem, maxMem, perm, 0));
        connect(m_checker.get(), &JavaChecker::checkFinished, this, [this](const JavaChecker::Result& r2) {
            if (r2.validity == JavaChecker::Result::Validity::Valid) {
                QString details = tr("Java %1 by %2 (%3).").arg(r2.javaVersion.toString(), r2.javaVendor, r2.realPlatform);
                if (!r2.errorLog.trimmed().isEmpty())
                    details += "\n\n" + tr("Warnings:") + "\n" + r2.errorLog.trimmed();
                finishJavaTest(true, tr("Java works"), details);
            } else {
                finishJavaTest(false, tr("Java doesn't accept these settings"),
                               tr("It starts, but fails with your memory or argument settings:") + "\n" + r2.errorLog.trimmed());
            }
        });
        // start after this handler returns: the previous checker is still emitting
        QTimer::singleShot(0, m_checker.get(), [c = m_checker] { c->start(); });
    });
    m_checker->start();
}

void SettingsBridge::downloadJava(const QString& instanceId)
{
    MinecraftInstance* inst = instanceId.isEmpty() ? nullptr : APPLICATION->instances()->getInstanceById(instanceId);
    auto* dialog = new Java::InstallDialog({}, inst, nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this] {
        m_javaScanning = false;
        scanJava();
    });
    dialog->open();
}

// ---- folders ----

QString SettingsBridge::folderFromUrl(const QUrl& url) const
{
    const QString raw = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (raw.isEmpty() || !QDir(raw).exists())
        return {};
    return FS::NormalizePath(raw);
}

QString SettingsBridge::instanceDirProblem(const QString& path) const
{
    if (path.isEmpty())
        return {};
    if (FS::checkProblemticPathJava(QDir(path)))
        return tr("This path contains a \"!\". Java has trouble with that, so your instances probably won't start.");
    if (DesktopServices::isFlatpak() && path.startsWith("/run/user"))
        return tr("This folder was only granted temporarily by Flatpak and may stop working after a restart. "
                  "Grant access to it with Flatseal instead.");
    return {};
}

void SettingsBridge::openFolder(const QString& path) const
{
    DesktopServices::openPath(path, true);
}
