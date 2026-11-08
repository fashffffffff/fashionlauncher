// SPDX-License-Identifier: GPL-3.0-only
#include "StorageScan.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "../store/ContentStore.h"
#include "../store/Snapshots.h"

namespace StorageScan {

namespace {

constexpr qint64 kMinAgeSecs = 24 * 3600;  // anything changed in the last day may still be in use

QString tr(const char* s)
{
    return QCoreApplication::translate("StorageScan", s);
}

struct Walked {
    File file;
    qint64 mtime = 0;  // secs since epoch
};

bool under(const QString& path, const QString& dir)
{
    return !dir.isEmpty() && path.startsWith(dir + '/');
}

// Regular files below dir, never through symlinks.
void walk(const QString& dir, QList<Walked>& out, QSet<QString>& seenPaths)
{
    if (!QFileInfo(dir).isDir()) {
        return;
    }
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString path = QDir::cleanPath(fi.absoluteFilePath());
        if (seenPaths.contains(path)) {
            continue;
        }
        const auto id = ContentStore::fileId(path);
        if (!id.valid()) {
            continue;
        }
        seenPaths.insert(path);
        out.append({ File{ path, id.size, id.dev, id.ino, id.nlink }, fi.lastModified().toSecsSinceEpoch() });
    }
}

// Space freed when exactly these files go: an inode counts once, and only if all of its links are in the list.
qint64 freedBytes(const QList<File>& files)
{
    QHash<QPair<quint64, quint64>, QPair<quint64, qint64>> seen;  // inode -> (links in list, size)
    QHash<QPair<quint64, quint64>, quint64> nlink;
    for (const auto& f : files) {
        const QPair<quint64, quint64> key{ f.dev, f.ino };
        auto& e = seen[key];
        e.first++;
        e.second = f.size;
        nlink[key] = f.nlink;
    }
    qint64 total = 0;
    for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
        if (it->first >= nlink.value(it.key(), 1)) {
            total += it->second;
        }
    }
    return total;
}

void pruneEmptyParents(const QString& path, const QString& stopAt)
{
    QDir d = QFileInfo(path).dir();
    while (under(d.absolutePath(), stopAt) && d.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        const QString name = d.dirName();
        if (!d.cdUp() || !d.rmdir(name)) {
            break;
        }
    }
}

bool isLeftover(const QString& name)
{
    static const QStringList kSuffixes = { ".bak", ".tmp", ".temp", ".part", "~" };
    for (const auto& s : kSuffixes) {
        if (name.endsWith(s, Qt::CaseInsensitive) && name.size() > s.size()) {
            return true;
        }
    }
    return false;
}

const InstanceInfo* instanceOf(const QString& path, const QList<InstanceInfo>& instances)
{
    for (const auto& inst : instances) {
        if (under(path, inst.instanceRoot)) {
            return &inst;
        }
    }
    return nullptr;
}

}  // namespace

bool isProtected(const QString& path, const QList<InstanceInfo>& instances)
{
    // A symlink anywhere on the way makes the canonical path differ.
    const QFileInfo fi(path);
    if (fi.isSymLink() || fi.canonicalFilePath() != QDir::cleanPath(path)) {
        return true;
    }
    for (const auto& inst : instances) {
        if (under(path, inst.gameRoot + "/saves") || under(path, inst.instanceRoot + "/saves")) {
            return true;
        }
    }
    return false;
}

Result scan(const Input& in, const Progress& progress)
{
    Result r;
    r.dataRoot = in.dataRoot;
    r.storeRoot = in.storeRoot;
    r.instances = in.instances;
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // ---- everything on disk, once ----
    progress(tr("Measuring folders…"), 0, 0);
    QList<Walked> all;
    QSet<QString> seenPaths;
    walk(in.dataRoot, all, seenPaths);
    for (const auto& inst : in.instances) {
        if (!under(inst.instanceRoot, in.dataRoot)) {
            walk(inst.instanceRoot, all, seenPaths);
        }
    }

    const QString libRoot = in.dataRoot + "/libraries";
    const QString assetsRoot = in.dataRoot + "/assets";
    const QString cacheRoot = in.dataRoot + "/cache";
    const QString logRoot = in.dataRoot + "/logs";

    // ---- where the space goes ----
    {
        QHash<QString, qint64> usage;
        QSet<QPair<quint64, quint64>> counted;
        for (const auto& w : all) {
            const QString& p = w.file.path;
            if (w.file.nlink > 1) {
                const QPair<quint64, quint64> key{ w.file.dev, w.file.ino };
                if (counted.contains(key)) {
                    continue;
                }
                counted.insert(key);
            }
            QString cat = "other";
            if (under(p, in.storeRoot)) {
                cat = "mods";
            } else if (under(p, libRoot) || under(p, assetsRoot) || under(p, in.dataRoot + "/meta") ||
                       under(p, in.dataRoot + "/versions")) {
                cat = "game";
            } else if (under(p, in.dataRoot + "/java")) {
                cat = "java";
            } else if (under(p, cacheRoot) || under(p, logRoot) || p == in.dataRoot + "/metacache") {
                cat = "logs";
            } else if (const auto* inst = instanceOf(p, in.instances)) {
                bool content = false;
                for (const auto& d : inst->contentDirs) {
                    content = content || under(p, d);
                }
                if (under(p, inst->gameRoot + "/saves")) {
                    cat = "worlds";
                } else if (content || under(p, inst->gameRoot + "/mods")) {
                    cat = "mods";
                } else if (under(p, inst->gameRoot + "/screenshots")) {
                    cat = "screenshots";
                } else if (under(p, inst->gameRoot + "/logs") || under(p, inst->gameRoot + "/crash-reports")) {
                    cat = "logs";
                }
            }
            usage[cat] += w.file.size;
            r.total += w.file.size;
        }
        for (const char* id : { "worlds", "mods", "game", "java", "screenshots", "logs", "other" }) {
            r.usage.append({ id, usage.value(id) });
        }
    }

    // ---- libraries nobody uses ----
    progress(tr("Looking for unused game files…"), 0, 0);
    {
        Category c{ "libraries" };
        if (!in.profilesOk) {
            c.available = false;
            c.note = "profiles:" + in.profilesProblem;
        } else {
            for (const auto& w : all) {
                if (under(w.file.path, libRoot) && !in.keepLibraries.contains(w.file.path) && now - w.mtime > kMinAgeSecs) {
                    c.files.append(w.file);
                }
            }
        }
        r.categories.append(c);
    }

    // ---- assets nobody uses ----
    {
        Category c{ "assets" };
        QSet<QString> keepHashes;
        if (!in.profilesOk) {
            c.available = false;
            c.note = "profiles:" + in.profilesProblem;
        }
        for (const auto& id : in.assetIds) {
            if (!c.available) {
                break;
            }
            QFile f(assetsRoot + "/indexes/" + id + ".json");
            QJsonParseError err;
            const auto doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &err) : QJsonDocument();
            const QJsonObject objects = doc.object().value("objects").toObject();
            if (doc.isNull() || objects.isEmpty()) {
                c.available = false;
                c.note = "assetIndex:" + id;
                break;
            }
            for (auto it = objects.begin(); it != objects.end(); ++it) {
                keepHashes.insert(it.value().toObject().value("hash").toString());
            }
        }
        if (c.available) {
            for (const auto& w : all) {
                const QString& p = w.file.path;
                if (!under(p, assetsRoot) || now - w.mtime <= kMinAgeSecs) {
                    continue;
                }
                const QString rel = p.mid(assetsRoot.size() + 1);
                const QString first = rel.section('/', 0, 0);
                bool drop = false;
                if (first == "objects") {
                    drop = !keepHashes.contains(QFileInfo(p).fileName());
                } else if (first == "indexes" && rel.count('/') == 1 && rel.endsWith(".json")) {
                    drop = !in.assetIds.contains(QFileInfo(p).completeBaseName());
                } else if (first == "virtual") {
                    drop = !in.assetIds.contains(rel.section('/', 1, 1));
                }
                if (drop) {
                    c.files.append(w.file);
                }
            }
        }
        r.categories.append(c);
    }

    // ---- download cache ----
    {
        Category c{ "cache" };
        for (const auto& w : all) {
            const QString& p = w.file.path;
            // cache/qmlui holds downloaded backgrounds, top-level files are other features' state: keep both.
            if (!under(p, cacheRoot) || under(p, cacheRoot + "/qmlui") || !p.mid(cacheRoot.size() + 1).contains('/')) {
                continue;
            }
            if (now - w.mtime > kMinAgeSecs) {
                c.files.append(w.file);
            }
        }
        r.categories.append(c);
    }

    // ---- old logs and crash reports ----
    {
        Category c{ "logs" };
        const qint64 maxAge = qint64(qMax(1, in.logDays)) * 24 * 3600;
        for (const auto& w : all) {
            const QString& p = w.file.path;
            bool isLog = under(p, logRoot);
            if (!isLog) {
                if (const auto* inst = instanceOf(p, in.instances)) {
                    isLog = under(p, inst->gameRoot + "/logs") || under(p, inst->gameRoot + "/crash-reports");
                }
            }
            if (isLog && now - w.mtime > maxAge) {
                c.files.append(w.file);
            }
        }
        r.categories.append(c);
    }

    // ---- leftovers ----
    {
        Category c{ "leftovers" };
        for (const auto& w : all) {
            const QString& p = w.file.path;
            const auto* inst = instanceOf(p, in.instances);
            if (!inst || now - w.mtime <= kMinAgeSecs || !isLeftover(QFileInfo(p).fileName())) {
                continue;
            }
            if (under(p, inst->gameRoot + "/saves") || under(p, inst->gameRoot + "/screenshots") ||
                under(p, inst->instanceRoot + "/.fashion")) {
                continue;
            }
            c.files.append(w.file);
        }
        r.categories.append(c);
    }

    // ---- duplicate content (linked into the store, not deleted) ----
    progress(tr("Comparing mods across instances…"), 0, 0);
    {
        Category c{ "duplicates" };
        c.lossless = true;
        ContentStore::Locker lock;
        const auto index = ContentStore::scan(in.storeRoot);
        QHash<qint64, QList<File>> bySize;
        for (const auto& inst : in.instances) {
            if (inst.running) {
                continue;
            }
            for (const auto& d : inst.contentDirs) {
                for (const auto& path : ContentStore::contentFiles(d)) {
                    const auto id = ContentStore::fileId(path);
                    if (id.valid() && id.size > 0) {
                        bySize[id.size].append(File{ QDir::cleanPath(path), id.size, id.dev, id.ino, id.nlink });
                    }
                }
            }
        }
        for (auto it = bySize.cbegin(); it != bySize.cend(); ++it) {
            QSet<QPair<quint64, quint64>> inodes;
            for (const auto& f : *it) {
                inodes.insert({ f.dev, f.ino });
            }
            if (inodes.size() < 2) {
                continue;
            }
            QHash<QString, QList<File>> bySha;
            for (const auto& f : *it) {
                const QString sha = ContentStore::sha256(f.path);
                if (!sha.isEmpty()) {
                    bySha[sha].append(f);
                }
            }
            for (auto s = bySha.cbegin(); s != bySha.cend(); ++s) {
                // Keep the inode the store already has, else the first copy.
                QPair<quint64, quint64> keeper{ s->first().dev, s->first().ino };
                if (index.bySha.contains(s.key())) {
                    const auto sid = ContentStore::fileId(index.bySha.value(s.key()).path);
                    keeper = { sid.dev, sid.ino };
                }
                QSet<QPair<quint64, quint64>> distinct;
                for (const auto& f : *s) {
                    distinct.insert({ f.dev, f.ino });
                }
                if (distinct.size() < 2 && distinct.contains(keeper)) {
                    continue;
                }
                for (const auto& f : *s) {
                    if (QPair<quint64, quint64>{ f.dev, f.ino } != keeper) {
                        c.files.append(f);
                    }
                }
            }
        }
        r.categories.append(c);
    }

    // ---- snapshots beyond the kept ones and store files nothing needs ----
    {
        Category c{ "snapshots" };
        ContentStore::Locker lock;
        QSet<QString> referenced;
        for (const auto& inst : in.instances) {
            const auto snaps = Snapshots::list(inst.instanceRoot);
            for (int i = 0; i < snaps.size(); i++) {
                const QString file = snaps[i].value("file").toString();
                if (i >= Snapshots::kKeep) {
                    const auto id = ContentStore::fileId(file);
                    if (id.valid()) {
                        c.files.append(File{ QDir::cleanPath(file), id.size, id.dev, id.ino, id.nlink });
                    }
                    continue;
                }
                const QJsonObject files = snaps[i].value("files").toObject();
                for (auto it = files.begin(); it != files.end(); ++it) {
                    for (const auto& v : it.value().toArray()) {
                        referenced.insert(v.toObject().value("sha256").toString());
                    }
                }
            }
        }
        const auto index = ContentStore::scan(in.storeRoot);
        for (auto it = index.bySha.cbegin(); it != index.bySha.cend(); ++it) {
            const auto id = ContentStore::fileId(it->path);
            if (id.valid() && id.nlink <= 1 && !referenced.contains(it.key())) {
                c.files.append(File{ QDir::cleanPath(it->path), id.size, id.dev, id.ino, id.nlink });
            }
        }
        r.categories.append(c);
    }

    // ---- screenshots (only on request) ----
    if (in.includeScreenshots) {
        Category c{ "screenshots" };
        for (const auto& w : all) {
            const auto* inst = instanceOf(w.file.path, in.instances);
            if (inst && under(w.file.path, inst->gameRoot + "/screenshots")) {
                c.files.append(w.file);
            }
        }
        r.categories.append(c);
    }

    for (auto& c : r.categories) {
        std::sort(c.files.begin(), c.files.end(), [](const File& a, const File& b) { return a.size > b.size; });
        if (c.id == "duplicates") {
            // Linking frees every copy but the kept one; the copies have no other links outside the instances.
            QSet<QPair<quint64, quint64>> seen;
            for (const auto& f : c.files) {
                if (!seen.contains({ f.dev, f.ino })) {
                    seen.insert({ f.dev, f.ino });
                    c.bytes += f.size;
                }
            }
        } else {
            c.bytes = freedBytes(c.files);
        }
    }
    return r;
}

CleanOutcome clean(const Result& last, const QStringList& ids, const Progress& progress)
{
    CleanOutcome out;
    int total = 0;
    for (const auto& c : last.categories) {
        if (ids.contains(c.id) && c.available) {
            total += c.files.size();
        }
    }
    int done = 0;

    for (const auto& c : last.categories) {
        if (!ids.contains(c.id) || !c.available || c.files.isEmpty()) {
            continue;
        }

        if (c.id == "duplicates") {
            progress(tr("Linking identical files into the store…"), done, total);
            ContentStore::Locker lock;
            QStringList dirs;
            for (const auto& f : c.files) {
                const QString d = QFileInfo(f.path).absolutePath();
                if (!dirs.contains(d)) {
                    dirs << d;
                }
            }
            // Measure the real difference: unique inode bytes of these folders before and after.
            auto measure = [&dirs] {
                QSet<QPair<quint64, quint64>> seen;
                qint64 bytes = 0;
                for (const auto& d : dirs) {
                    for (const auto& p : ContentStore::contentFiles(d)) {
                        const auto id = ContentStore::fileId(p);
                        if (id.valid() && !seen.contains({ id.dev, id.ino })) {
                            seen.insert({ id.dev, id.ino });
                            bytes += id.size;
                        }
                    }
                }
                return bytes;
            };
            const qint64 before = measure();
            auto index = ContentStore::scan(last.storeRoot);
            ContentStore::process(dirs, index, true, 60);
            out.freed += qMax<qint64>(0, before - measure());
            done += c.files.size();
            progress({}, done, total);
            continue;
        }

        if (c.id == "snapshots") {
            progress(tr("Removing old snapshots…"), done, total);
            ContentStore::Locker lock;
            QStringList roots;
            for (const auto& inst : last.instances) {
                roots << inst.instanceRoot;
            }
            for (const auto& f : c.files) {
                if (f.path.endsWith(".json") && !under(f.path, last.storeRoot)) {
                    const auto id = ContentStore::fileId(f.path);
                    if (id.valid() && id.ino == f.ino && QFile::remove(f.path)) {
                        out.freed += f.size;
                    }
                }
            }
            auto index = ContentStore::scan(last.storeRoot);
            out.freed += ContentStore::gc(index, Snapshots::referenced(roots));
            done += c.files.size();
            progress({}, done, total);
            continue;
        }

        const QString status = tr("Removing files…");
        progress(status, done, total);
        const QString stopAt = c.id == "libraries" ? last.dataRoot + "/libraries"
                               : c.id == "assets"  ? last.dataRoot + "/assets"
                               : c.id == "cache"   ? last.dataRoot + "/cache"
                                                   : QString();
        QList<File> removed;
        for (const auto& f : c.files) {
            done++;
            // Re-check everything: the file must still be the one we measured, and never a world or a link.
            const auto id = ContentStore::fileId(f.path);
            if (!id.valid() || id.ino != f.ino || id.dev != f.dev || isProtected(f.path, last.instances)) {
                continue;
            }
            if (QFile::remove(f.path)) {
                removed.append(File{ f.path, id.size, id.dev, id.ino, id.nlink });
                if (!stopAt.isEmpty()) {
                    pruneEmptyParents(f.path, stopAt);
                }
            } else {
                out.failed++;
            }
            if (done % 200 == 0) {
                progress(status, done, total);
            }
        }
        out.freed += freedBytes(removed);
    }
    return out;
}

}  // namespace StorageScan
