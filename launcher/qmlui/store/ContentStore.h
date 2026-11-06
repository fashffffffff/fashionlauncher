// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * Hash-addressed store of instance content (mods, resource packs, shader packs).
 *
 * Layout: <data>/store/<first two hex>/<sha256>.<ext>. Instance files stay ordinary files; when a file's content is
 * already in the store it is replaced by a HARD link to the store file, so identical files take disk space once.
 * Deleting or replacing a file in one instance only removes that instance's link; other instances keep theirs.
 * A store file whose only link is the store itself, and that no snapshot needs, is deleted by gc().
 *
 * Everything here works on plain paths and is safe to call from a worker thread. Callers must hold lock().
 */
namespace ContentStore {

struct FileId {
    quint64 dev = 0;
    quint64 ino = 0;
    quint64 nlink = 0;
    qint64 size = -1;
    bool valid() const { return size >= 0; }
};

struct Entry {
    QString path;  // store file
    quint64 nlink = 0;
    qint64 size = 0;
};

// What is in the store right now. Built by scan(), kept up to date by process()/link calls.
struct Index {
    QString root;
    QHash<QPair<quint64, quint64>, QString> shaByInode;
    QHash<QString, Entry> bySha;
};

// Serialises every store operation (worker passes, launch hook, restore).
void lock();
void unlock();
struct Locker {
    Locker() { lock(); }
    ~Locker() { unlock(); }
};

QString root(const QString& dataRoot);
FileId fileId(const QString& path);
QString sha256(const QString& path);  // cached by path + size + mtime
QString storeExt(const QString& fileName);

Index scan(const QString& root);

// Top-level content files of a folder (regular, not hidden, not links).
QStringList contentFiles(const QString& dir);

/**
 * Puts every content file of the given folders into the store and returns path -> sha256 for each of them.
 * When share is on, a file whose content is already stored under another inode is replaced by a hard link.
 * Files changed less than minAgeSecs ago are skipped (they may still be being written).
 * Files on another volume can't be linked and stay as they are (still hashed).
 */
QHash<QString, QString> process(const QStringList& dirs, Index& index, bool share, int minAgeSecs, int* linkedOut = nullptr);

// Hard-links (or, across volumes, copies) the stored file into dest. Replaces dest atomically.
bool materialize(const Index& index, const QString& sha, const QString& dest);
// Adds a file to the store by hard link (no copy). Returns false if it can't be linked.
bool ingest(Index& index, const QString& path, const QString& sha);

// Store files saved bytes and number of instances (gameRoots) that share at least one file.
struct Stats {
    qint64 savedBytes = 0;
    int instances = 0;
    int files = 0;
};
Stats stats(const Index& index, const QList<QStringList>& dirsPerInstance);

// Deletes store files with a single link that no snapshot references. Returns bytes freed.
qint64 gc(Index& index, const QSet<QString>& referenced);

}  // namespace ContentStore
