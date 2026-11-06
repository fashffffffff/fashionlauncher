// SPDX-License-Identifier: GPL-3.0-only
#include "ToolsBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

#include "LaunchCheck.h"
#include "LauncherImport.h"
#include "PackSettings.h"

ToolsBridge* ToolsBridge::s_instance = nullptr;

namespace {
const QString kChecksSetting = QStringLiteral("QmlPreLaunchChecks");

MinecraftInstance* findInstance(const QString& id)
{
    return APPLICATION->instances()->getInstanceById(id);
}

QString localPath(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}
}  // namespace

ToolsBridge::ToolsBridge(QObject* parent) : QObject(parent)
{
    s_instance = this;
    auto settings = APPLICATION->settings();
    if (!settings->contains(kChecksSetting)) {
        settings->registerSetting(kChecksSetting, true);
    }
    // Automation for snapshots: FFPRISM_TOOLS=check:<instance id> | launchers | folder:<path>
    const QString automation = qEnvironmentVariable("FFPRISM_TOOLS");
    if (!automation.isEmpty()) {
        QTimer::singleShot(1500, this, [this, automation] {
            if (automation.startsWith("check:")) {
                const QString id = automation.mid(6);
                const auto problems = check(id);
                for (const auto& p : problems) {
                    qInfo() << "[tools] problem:" << p.toMap().value("title").toString() << "|" << p.toMap().value("fixLabel").toString();
                }
                if (!problems.isEmpty()) {
                    emit launchBlocked(id, APPLICATION->instances()->getInstanceById(id)->name(), false, problems);
                }
            } else if (automation.startsWith("writepack:")) {  // writepack:<id>|<archive or empty>
                const auto parts = automation.mid(10).split('|');
                qInfo() << "[tools] writePackSettings ->" << writePackSettings(parts.value(0), parts.value(1));
            } else if (automation == "launchers") {
                refreshLaunchers();
            } else if (automation.startsWith("folder:") || automation.startsWith("import:")) {
                scanFolder(automation.mid(7));
            }
        });
    }
}

ToolsBridge::~ToolsBridge()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool ToolsBridge::checksEnabled() const
{
    return APPLICATION->settings()->get(kChecksSetting).toBool();
}

void ToolsBridge::setChecksEnabled(bool on)
{
    if (on == checksEnabled()) {
        return;
    }
    APPLICATION->settings()->set(kChecksSetting, on);
    emit checksEnabledChanged();
}

// ---- pre-launch checks ---------------------------------------------------------------------

bool ToolsBridge::holdLaunch(const QString& id, bool offline)
{
    if (m_allowOnce.remove(id) || !checksEnabled()) {
        return false;
    }
    auto* inst = findInstance(id);
    if (!inst || inst->isRunning()) {
        return false;
    }
    const QVariantList problems = LaunchCheck::run(inst);
    if (problems.isEmpty()) {
        return false;
    }
    emit launchBlocked(id, inst->name(), offline, problems);
    return true;
}

QVariantList ToolsBridge::check(const QString& id) const
{
    return LaunchCheck::run(findInstance(id));
}

void ToolsBridge::allowOnce(const QString& id)
{
    m_allowOnce.insert(id);
}

int ToolsBridge::disableFiles(const QStringList& paths)
{
    int done = 0;
    for (const auto& path : paths) {
        if (!QFileInfo(path).isFile()) {
            continue;
        }
        QString target = path + ".disabled";
        if (QFileInfo::exists(target)) {
            QFile::remove(target);  // an older turned-off copy of the very same file
        }
        if (QFile::rename(path, target)) {
            ++done;
        } else {
            qWarning() << "[tools] couldn't turn off" << path;
        }
    }
    return done;
}

int ToolsBridge::autoFix(const QVariantList& problems)
{
    QStringList files;
    for (const auto& p : problems) {
        const auto m = p.toMap();
        if (m.value("autoFix").toBool() && m.value("fixAction").toString() == "disable") {
            files << m.value("fixArg").toStringList();
        }
    }
    files.removeDuplicates();
    return disableFiles(files);
}

void ToolsBridge::requestFind(const QString& id, const QString& query)
{
    m_pendingFind.insert(id, query);
}

// ---- other launchers -----------------------------------------------------------------------

void ToolsBridge::runScan(std::function<QVariantList()> scan, const QString& folder)
{
    const int generation = ++m_scanGeneration;
    m_scanning = true;
    m_scannedFolder = folder;
    emit launchersChanged();
    auto* watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_scanGeneration) {
            return;
        }
        m_launchers = watcher->result();
        m_scanning = false;
        qInfo() << "[tools] found" << m_launchers.size() << "instance(s) of other launchers";
        emit launchersChanged();
    });
    watcher->setFuture(QtConcurrent::run(std::move(scan)));
}

void ToolsBridge::refreshLaunchers()
{
    runScan([] { return LauncherImport::detect(); }, QString());
}

void ToolsBridge::scanFolder(const QString& pathOrUrl)
{
    const QString path = localPath(pathOrUrl);
    runScan([path] { return LauncherImport::scanFolder(path); }, path);
}

QVariantMap ToolsBridge::launcherEntry(const QString& key) const
{
    for (const auto& v : m_launchers) {
        const auto m = v.toMap();
        if (m.value("key").toString() == key) {
            return m;
        }
    }
    return {};
}

// ---- modpack settings ----------------------------------------------------------------------

QVariantMap ToolsBridge::packSettings(const QString& id) const
{
    return PackSettings::fromInstance(findInstance(id)).toVariantMap();
}

QString ToolsBridge::writePackSettings(const QString& id, const QString& archivePath) const
{
    auto* inst = findInstance(id);
    if (!inst) {
        return tr("This instance no longer exists.");
    }
    const QJsonObject settings = PackSettings::fromInstance(inst);
    if (settings.isEmpty()) {
        return {};  // nothing beyond the defaults to recommend
    }
    if (archivePath.isEmpty()) {
        QFile f(QDir(inst->instanceRoot()).filePath(PackSettings::FileName));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return tr("Couldn't write %1.").arg(f.fileName());
        }
        f.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
        return {};
    }
    return PackSettings::writeIntoArchive(localPath(archivePath), settings);
}

QString ToolsBridge::applyPackSettings(BaseInstance* instance, const QString& archivePath)
{
    if (!instance || archivePath.isEmpty()) {
        return {};
    }
    const QJsonObject settings = PackSettings::readFromArchive(archivePath);
    if (settings.isEmpty()) {
        return {};
    }
    const QString said = PackSettings::apply(instance, settings);
    qInfo() << "[tools] applied modpack settings to" << instance->id() << ":" << said;
    return said;
}

void ToolsBridge::announce(const QString& message)
{
    if (message.isEmpty()) {
        return;
    }
    // after whatever the caller shows right now (e.g. "… is ready")
    QTimer::singleShot(50, [message] {
        if (s_instance) {
            emit s_instance->notice(message);
        }
    });
}
