// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

/**
 * Disk usage scan and cleanup, on plain paths (runs on a worker thread). What may be removed:
 *  - libraries: files under <data>/libraries that no instance's resolved launch profile names (jars, natives of both
 *    architectures, main jar, jar mods, maven files, agents). Skipped entirely if any instance failed to resolve.
 *  - assets: objects not listed by the asset index of any instance, plus unused index files and virtual folders.
 *    Skipped entirely if a used index is missing or unreadable (we can't know what it needs).
 *  - cache: <data>/cache (HTTP cache; HttpMetaCache treats missing files as stale). meta/ is never touched: offline
 *    instances need it to start.
 *  - logs: instance logs/ and crash-reports/ and launcher logs/ older than N days (current logs are always newer).
 *  - leftovers: *.bak, *.tmp, *.temp, *.part, *~ in instances, outside saves and screenshots.
 *  - duplicates: identical content files in different instances that are separate copies. Not deleted: linked into the
 *    content store (lossless).
 *  - snapshots: snapshot files beyond the kept ones, and store files nothing links to or needs.
 *  - screenshots: only when asked for, never preselected.
 * Never: anything under a saves/ folder, anything reached through a symlink, anything changed in the last day.
 * Sizes are hard-link aware: a file only frees space when every link to it goes.
 */
namespace StorageScan {

struct InstanceInfo {
    QString id;
    QString name;
    QString instanceRoot;
    QString gameRoot;
    QStringList contentDirs;
    bool running = false;
};

struct Input {
    QString dataRoot;
    QString storeRoot;
    QList<InstanceInfo> instances;
    QSet<QString> keepLibraries;  // absolute clean paths
    QSet<QString> assetIds;
    bool profilesOk = true;
    QString profilesProblem;  // instance name that failed
    int logDays = 30;
    bool includeScreenshots = false;
};

struct File {
    QString path;
    qint64 size = 0;
    quint64 dev = 0, ino = 0, nlink = 1;
};

struct Category {
    QString id;
    QList<File> files;
    qint64 bytes = 0;      // space actually freed (hard-link aware)
    bool available = true;
    QString note;          // why not available, machine-readable ("profiles:<name>", "assetIndex:<id>")
    bool lossless = false; // duplicates
};

struct Usage {
    QString id;  // worlds, mods, game, java, screenshots, logs, other
    qint64 bytes = 0;
};

struct Result {
    QList<Usage> usage;
    qint64 total = 0;
    QList<Category> categories;
    QString dataRoot;
    QString storeRoot;
    QList<InstanceInfo> instances;
};

using Progress = std::function<void(const QString& status, int done, int total)>;

Result scan(const Input& in, const Progress& progress);

struct CleanOutcome {
    qint64 freed = 0;
    int failed = 0;
};
CleanOutcome clean(const Result& last, const QStringList& ids, const Progress& progress);

// True for paths that must never be deleted (inside saves/, symlinks on the way).
bool isProtected(const QString& path, const QList<InstanceInfo>& instances);

}  // namespace StorageScan
