// SPDX-License-Identifier: GPL-3.0-only
#include "BlockedModsBridge.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDirListing>
#include <QEventLoop>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#include "Application.h"
#include "modplatform/helpers/HashUtils.h"
#include "settings/SettingsObject.h"

#include "BlockedModsMatcher.h"

// CurseForge gives its blocked files as sha1 sums (BlockedModsDialog's default hasher).
static const char* s_hashType = "sha1";

BlockedModsBridge::BlockedModsBridge(QObject* parent) : QObject(parent) {}

BlockedModsBridge::~BlockedModsBridge()
{
    // The window is going away: an open question counts as dismissed, so the install aborts.
    if (m_loop) {
        m_accepted = false;
        m_loop->quit();
    }
}

bool BlockedModsBridge::askForMods(QList<BlockedMod>& mods)
{
    if (m_active) {
        qWarning() << "[Blocked Mods] a blocked-mods question is already open, refusing";
        return false;
    }

    m_mods = mods;
    m_active = true;
    m_accepted = false;

    emit modsChanged();
    emit requested();
    // Defer folder watching until after the dialog appears, so macOS permission prompts
    // show once the window they belong to is visible (same as the classic dialog).
    QTimer::singleShot(0, this, [this] {
        if (m_active) {
            setupWatch();
            scanPaths();
        }
    });

    QEventLoop loop;
    m_loop = &loop;
    QPointer<BlockedModsBridge> self(this);
    loop.exec(QEventLoop::DialogExec);
    if (!self) {
        return false;  // the shell was destroyed under us: cancelled
    }
    m_loop = nullptr;

    stopWatching();

    if (m_accepted) {
        mods = m_mods;
    }
    return m_accepted;
}

void BlockedModsBridge::preview()
{
    if (m_active) {
        return;
    }

    BlockedMod a;
    a.name = "sodium-fabric-0.6.13+mc1.21.1.jar";
    a.websiteUrl = "https://www.curseforge.com/minecraft/mc-mods/sodium/download/6339138";
    a.hash = "8f4a1c55b2e0d3f6a9c8e7b1d2f0a3c4e5b6d7f8";
    a.matched = false;
    BlockedMod b;
    b.name = "Oculus-mc1.21.1-1.8.0.jar";
    b.websiteUrl = "https://www.curseforge.com/minecraft/mc-mods/oculus/download/6211238";
    b.hash = "1a2b3c4d5e6f70819a2b3c4d5e6f70819a2b3c4d";
    b.matched = false;
    BlockedMod c;
    c.name = "farmersdelight-1.21.1-1.3.2.jar";
    c.websiteUrl = "https://www.curseforge.com/minecraft/mc-mods/farmers-delight/download/6338392";
    c.hash = "5e6f70819a2b3c4d5e6f70819a2b3c4d5e6f7081";
    c.matched = false;

    m_mods = { a, b, c };
    m_active = true;
    m_accepted = false;
    emit modsChanged();
    emit requested();
    // Same deferred watching as a real question, so the debug page exercises the real
    // detection path (set DownloadsDir in the test data to keep it off the system folder).
    QTimer::singleShot(0, this, [this] {
        if (m_active) {
            setupWatch();
            scanPaths();
        }
    });
}

QVariantList BlockedModsBridge::mods() const
{
    QVariantList out;
    for (const auto& mod : m_mods) {
        out.append(QVariantMap{ { "name", mod.name },
                                { "websiteUrl", mod.websiteUrl },
                                { "hash", mod.hash },
                                { "size", 0 },
                                { "found", mod.matched },
                                { "localPath", mod.localPath },
                                { "disabled", mod.disabled } });
    }
    return out;
}

bool BlockedModsBridge::allFound() const
{
    return BlockedModsMatcher::allFound(m_mods);
}

int BlockedModsBridge::foundCount() const
{
    int found = 0;
    for (const auto& mod : m_mods) {
        if (mod.matched) {
            found++;
        }
    }
    return found;
}

void BlockedModsBridge::openPage(int index)
{
    if (index < 0 || index >= m_mods.size()) {
        return;
    }
    QDesktopServices::openUrl(QUrl(m_mods[index].websiteUrl));
}

void BlockedModsBridge::openAll()
{
    for (const auto& mod : m_mods) {
        if (!mod.matched) {
            QDesktopServices::openUrl(QUrl(mod.websiteUrl));
        }
    }
}

void BlockedModsBridge::addFolder(const QUrl& url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    watchPath(path);
    scanPath(path);
    runHashTasks();
    emit modsChanged();
}

void BlockedModsBridge::pickFile(int index, const QUrl& url)
{
    if (!url.isLocalFile()) {
        return;
    }
    Q_UNUSED(index);
    queueHashTask(url.toLocalFile());
    runHashTasks();
}

void BlockedModsBridge::confirm()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    m_accepted = true;
    stopWatching();
    emit finished();
    emit modsChanged();
    if (m_loop) {
        m_loop->quit();
    }
}

void BlockedModsBridge::cancel()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    m_accepted = false;
    stopWatching();
    emit finished();
    emit modsChanged();
    if (m_loop) {
        m_loop->quit();
    }
}

QString BlockedModsBridge::downloadsDir() const
{
    const QString dir = APPLICATION->settings()->get("DownloadsDir").toString();
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : dir;
}

bool BlockedModsBridge::moveFromDownloads() const
{
    return APPLICATION->settings()->get("MoveModsFromDownloadsDir").toBool();
}

/// @brief add the user's downloads folder and the global mods folder to the watcher,
/// like the classic dialog does.
void BlockedModsBridge::setupWatch()
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        validateMatchedMods();
        scanPath(path);
        runHashTasks();
    });

    const bool downloadsRecursive = APPLICATION->settings()->get("DownloadsDirWatchRecursive").toBool();
    watchPath(downloadsDir(), downloadsRecursive);
    watchPath(APPLICATION->settings()->get("CentralModsDir").toString(), true);
}

void BlockedModsBridge::watchPath(const QString& path, bool watchRecursive)
{
    auto toWatch = QFileInfo(path);
    if (!toWatch.isReadable()) {
        qWarning() << "[Blocked Mods] Failed to add Watch Path (unable to read):" << path;
        return;
    }
    auto toWatchPath = toWatch.canonicalFilePath();
    if (m_watcher.directories().contains(toWatchPath)) {
        return;  // don't watch the same path twice (no loops!)
    }
    m_watcher.addPath(toWatchPath);

    if (!toWatch.isDir() || !watchRecursive) {
        return;
    }

    for (const auto& entry : QDirListing(toWatchPath, QDirListing::IteratorFlag::DirsOnly | QDirListing::IteratorFlag::ResolveSymlinks)) {
        watchPath(entry.canonicalFilePath(), watchRecursive);  // resolve symlinks and relative paths
    }
}

void BlockedModsBridge::scanPaths()
{
    for (const auto& dir : m_watcher.directories()) {
        scanPath(dir);
    }
    runHashTasks();
}

void BlockedModsBridge::scanPath(const QString& path)
{
    for (const auto& entry : QDirListing(path, QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::ResolveSymlinks |
                                                   QDirListing::IteratorFlag::IncludeHidden)) {
        const QString file = entry.absoluteFilePath();
        if (!BlockedModsMatcher::matchByName(m_mods, file, downloadsDir(), moveFromDownloads())) {
            continue;  // no name match: not worth hashing
        }
        queueHashTask(file);
    }
    runHashTasks();
}

/// @brief add a hashing task for the file located at path, or queue it if one is already running
void BlockedModsBridge::queueHashTask(const QString& path)
{
    if (!m_hashingTask) {
        m_hashingTask = shared_qobject_ptr<ConcurrentTask>(
            new ConcurrentTask("BlockedModsHashing", APPLICATION->settings()->get("NumberOfConcurrentTasks").toInt()));
        connect(m_hashingTask.get(), &Task::finished, this, &BlockedModsBridge::hashTasksFinished);
    }
    m_pendingHashPaths.insert(path);
}

void BlockedModsBridge::runHashTasks()
{
    if (!m_hashingTask) {
        return;
    }

    if (!m_hashingTask->isRunning()) {
        m_rehashPending = false;
        if (m_pendingHashPaths.isEmpty()) {
            return;
        }
        auto path = m_pendingHashPaths.begin();
        while (path != m_pendingHashPaths.end()) {
            const QString p = *path;
            auto hasher = Hashing::createHasher(p, s_hashType);
            connect(hasher.get(), &Task::succeeded, this, [this, hasher, p] {
                if (BlockedModsMatcher::matchByHash(m_mods, hasher->getResult(), p, downloadsDir(), moveFromDownloads())) {
                    emit modsChanged();
                }
            });
            connect(hasher.get(), &Task::failed, this, [p] { qDebug() << "[Blocked Mods] Failed to hash path:" << p; });
            m_hashingTask->addTask(hasher);
            path = m_pendingHashPaths.erase(path);
        }
        m_hashingTask->start();
    } else {
        m_rehashPending = true;
    }
}

void BlockedModsBridge::hashTasksFinished()
{
    if (m_rehashPending) {
        runHashTasks();
    }
}

/// @brief ensure matched file paths still exist
void BlockedModsBridge::validateMatchedMods()
{
    bool changed = false;
    for (auto& mod : m_mods) {
        if (mod.matched) {
            QFileInfo file = QFileInfo(mod.localPath);
            if (!file.exists() || !file.isFile()) {
                qDebug() << "[Blocked Mods] File" << mod.localPath << "for mod" << mod.name << "has vanished! marking as not matched.";
                mod.localPath = "";
                mod.matched = false;
                changed = true;
            }
        }
    }
    if (changed) {
        emit modsChanged();
    }
}

void BlockedModsBridge::stopWatching()
{
    disconnect(&m_watcher, nullptr, this, nullptr);
    m_watcher.removePaths(m_watcher.directories());
    if (m_hashingTask) {
        m_hashingTask->disconnect(this);
        if (m_hashingTask->isRunning()) {
            m_hashingTask->abort();
        }
        m_hashingTask.reset();
    }
    m_pendingHashPaths.clear();
    m_rehashPending = false;
}
