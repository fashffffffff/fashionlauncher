// SPDX-License-Identifier: GPL-3.0-only
#include "ContentStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRecursiveMutex>
#include <QRegularExpression>

#include <filesystem>
#include <system_error>

#include "StringUtils.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace ContentStore {

namespace {
QRecursiveMutex g_mutex;

struct CachedHash {
    qint64 size;
    qint64 mtime;
    QString sha;
};
QHash<QString, CachedHash> g_hashCache;

fs::path toPath(const QString& p)
{
    return fs::path(StringUtils::toStdString(QDir::toNativeSeparators(p)));
}

QString tempNameFor(const QString& dest)
{
    QFileInfo fi(dest);
    return fi.absoluteDir().filePath(QStringLiteral(".%1.fashion-tmp").arg(fi.fileName()));
}

// Moves src over dest in one step, so the folder never shows a missing file.
bool replaceWith(const QString& src, const QString& dest)
{
    std::error_code ec;
    fs::rename(toPath(src), toPath(dest), ec);
    if (ec) {
        qWarning() << "Store: could not replace" << dest << QString::fromStdString(ec.message());
        fs::remove(toPath(src), ec);
        return false;
    }
    return true;
}
}  // namespace

void lock()
{
    g_mutex.lock();
}
void unlock()
{
    g_mutex.unlock();
}

QString root(const QString& dataRoot)
{
    return QDir(dataRoot).filePath("store");
}

FileId fileId(const QString& path)
{
    FileId id;
#ifdef Q_OS_WIN
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return id;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle(h, &info)) {
        id.dev = info.dwVolumeSerialNumber;
        id.ino = (quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
        id.nlink = info.nNumberOfLinks;
        id.size = qint64((quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
    }
    CloseHandle(h);
#else
    struct stat st;
    if (::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISREG(st.st_mode)) {
        id.dev = quint64(st.st_dev);
        id.ino = quint64(st.st_ino);
        id.nlink = quint64(st.st_nlink);
        id.size = qint64(st.st_size);
    }
#endif
    return id;
}

QString sha256(const QString& path)
{
    QFileInfo fi(path);
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
    {
        QMutexLocker l(&g_mutex);
        auto it = g_hashCache.constFind(path);
        if (it != g_hashCache.cend() && it->size == fi.size() && it->mtime == mtime) {
            return it->sha;
        }
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) {
        return {};
    }
    const QString sha = QString::fromLatin1(h.result().toHex());
    QMutexLocker l(&g_mutex);
    g_hashCache.insert(path, { fi.size(), mtime, sha });
    return sha;
}

QString storeExt(const QString& fileName)
{
    QString n = fileName;
    if (n.endsWith(".disabled", Qt::CaseInsensitive)) {
        n.chop(9);
    }
    const QString ext = QFileInfo(n).suffix().toLower();
    static const QRegularExpression ok("^[a-z0-9]{1,8}$");
    return ok.match(ext).hasMatch() ? ext : QStringLiteral("bin");
}

Index scan(const QString& rootPath)
{
    Index idx;
    idx.root = rootPath;
    QDirIterator it(rootPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString sha = QFileInfo(path).completeBaseName();
        if (sha.size() != 64) {
            continue;
        }
        const FileId id = fileId(path);
        if (!id.valid()) {
            continue;
        }
        idx.shaByInode.insert({ id.dev, id.ino }, sha);
        idx.bySha.insert(sha, { path, id.nlink, id.size });
    }
    return idx;
}

QStringList contentFiles(const QString& dir)
{
    QStringList out;
    const auto infos = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);  // hidden files excluded
    for (const auto& fi : infos) {
        if (fi.isSymLink() || fi.fileName().endsWith(".fashion-tmp") || fi.fileName().endsWith(".part")) {
            continue;
        }
        out << fi.absoluteFilePath();
    }
    return out;
}

bool ingest(Index& index, const QString& path, const QString& sha)
{
    const QString ext = storeExt(QFileInfo(path).fileName());
    const QString dest = QDir(index.root).filePath(QStringLiteral("%1/%2.%3").arg(sha.left(2), sha, ext));
    QDir().mkpath(QFileInfo(dest).absolutePath());
    std::error_code ec;
    fs::create_hard_link(toPath(path), toPath(dest), ec);
    if (ec) {
        // Another volume, or a file system without hard links: leave the file alone.
        return false;
    }
    const FileId id = fileId(dest);
    index.shaByInode.insert({ id.dev, id.ino }, sha);
    index.bySha.insert(sha, { dest, id.nlink, id.size });
    return true;
}

QHash<QString, QString> process(const QStringList& dirs, Index& index, bool share, int minAgeSecs, int* linkedOut)
{
    QHash<QString, QString> out;
    int linked = 0;
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-minAgeSecs);
    for (const QString& dir : dirs) {
        for (const QString& path : contentFiles(dir)) {
            const FileId id = fileId(path);
            if (!id.valid() || id.size == 0) {
                continue;
            }
            // Already a link to a stored file: nothing to hash.
            if (auto known = index.shaByInode.constFind({ id.dev, id.ino }); known != index.shaByInode.cend()) {
                out.insert(path, *known);
                continue;
            }
            if (minAgeSecs > 0 && QFileInfo(path).lastModified() > cutoff) {
                continue;  // may still be downloading
            }
            const QString sha = sha256(path);
            if (sha.isEmpty()) {
                continue;
            }
            out.insert(path, sha);
            auto stored = index.bySha.find(sha);
            if (stored == index.bySha.end()) {
                ingest(index, path, sha);
                continue;
            }
            if (!share || stored->size != id.size) {
                continue;
            }
            // Same content stored under another inode: swap this copy for a link to the stored file.
            const QString tmp = tempNameFor(path);
            std::error_code ec;
            fs::remove(toPath(tmp), ec);
            fs::create_hard_link(toPath(stored->path), toPath(tmp), ec);
            if (ec) {
                continue;  // other volume: keep the copy
            }
            if (replaceWith(tmp, path)) {
                stored->nlink++;
                linked++;
            }
        }
    }
    if (linkedOut) {
        *linkedOut = linked;
    }
    return out;
}

bool materialize(const Index& index, const QString& sha, const QString& dest)
{
    auto stored = index.bySha.constFind(sha);
    if (stored == index.bySha.cend()) {
        return false;
    }
    QDir().mkpath(QFileInfo(dest).absolutePath());
    const QString tmp = tempNameFor(dest);
    std::error_code ec;
    fs::remove(toPath(tmp), ec);
    fs::create_hard_link(toPath(stored->path), toPath(tmp), ec);
    if (ec) {
        ec.clear();
        fs::copy_file(toPath(stored->path), toPath(tmp), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return false;
        }
    }
    return replaceWith(tmp, dest);
}

Stats stats(const Index& index, const QList<QStringList>& dirsPerInstance)
{
    Stats s;
    for (const auto& e : index.bySha) {
        // One link is the store's own; one more is the copy you'd have anyway.
        if (e.nlink >= 3) {
            s.savedBytes += e.size * qint64(e.nlink - 2);
            s.files++;
        }
    }
    for (const auto& dirs : dirsPerInstance) {
        bool shares = false;
        for (const QString& dir : dirs) {
            for (const QString& path : contentFiles(dir)) {
                const FileId id = fileId(path);
                if (id.valid() && id.nlink >= 3 && index.shaByInode.contains({ id.dev, id.ino })) {
                    shares = true;
                    break;
                }
            }
            if (shares) {
                break;
            }
        }
        s.instances += shares ? 1 : 0;
    }
    return s;
}

qint64 gc(Index& index, const QSet<QString>& referenced)
{
    qint64 freed = 0;
    for (auto it = index.bySha.begin(); it != index.bySha.end();) {
        // Re-read the link count: instances may have dropped their copy since the scan.
        const FileId id = fileId(it->path);
        if (id.valid() && id.nlink <= 1 && !referenced.contains(it.key())) {
            if (QFile::remove(it->path)) {
                freed += id.size;
                index.shaByInode.remove({ id.dev, id.ino });
                QDir().rmdir(QFileInfo(it->path).absolutePath());  // only succeeds when empty
                it = index.bySha.erase(it);
                continue;
            }
        } else if (id.valid()) {
            it->nlink = id.nlink;
        }
        ++it;
    }
    return freed;
}

}  // namespace ContentStore
