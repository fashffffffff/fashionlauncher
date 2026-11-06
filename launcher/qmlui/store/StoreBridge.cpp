// SPDX-License-Identifier: GPL-3.0-only
#include "StoreBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QtConcurrent>

#include "Application.h"
#include "BuildConfig.h"
#include "ContentStore.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "Snapshots.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"
#include "settings/SettingsObject.h"

StoreBridge* StoreBridge::s_self = nullptr;

namespace {
const QString kShareSetting = QStringLiteral("StoreShareFiles");

QString storeRoot()
{
    return ContentStore::root(APPLICATION->dataRoot());
}

struct InstanceRef {
    QString id;
    QString root;
    QStringList dirs;
    bool running;
};

QList<InstanceRef> allInstances()
{
    QList<InstanceRef> out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); i++) {
        auto* inst = list->at(i);
        out << InstanceRef{ inst->id(), inst->instanceRoot(), Snapshots::contentDirs(inst), inst->isRunning() };
    }
    return out;
}

QString enabledName(QString name)
{
    if (name.endsWith(".disabled", Qt::CaseInsensitive)) {
        name.chop(9);
    }
    return name;
}

void writeIndex(const QString& dir, const QJsonObject& e)
{
    const QString index = e.value("index").toString();
    const QString text = e.value("indexText").toString();
    if (index.isEmpty() || text.isEmpty() || index.contains('/') || index.contains('\\')) {
        return;
    }
    const QDir indexDir(QDir(dir).filePath(".index"));
    QDir().mkpath(indexDir.absolutePath());
    if (auto res = FS::write(indexDir.filePath(index), text.toUtf8()); !res) {
        qWarning() << "Store: could not write metadata" << index << res.error();
    }
}
}  // namespace

StoreBridge::StoreBridge(QObject* parent) : QObject(parent)
{
    s_self = this;
    auto settings = APPLICATION->settings();
    if (!settings->contains(kShareSetting)) {
        settings->registerSetting(kShareSetting, true);
    }

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(4000);
    connect(&m_debounce, &QTimer::timeout, this, &StoreBridge::runPass);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        const QString clean = QDir::cleanPath(path);
        for (const auto& ref : allInstances()) {
            for (const auto& d : ref.dirs) {
                if (QDir::cleanPath(d) == clean) {
                    schedulePass(ref.id);
                    return;
                }
            }
        }
    });
    connect(APPLICATION->instances(), &InstanceList::instancesChanged, this, [this] {
        rewatch();
        schedulePass();
    });
    rewatch();
    schedulePass();  // first full pass shortly after start
}

StoreBridge::~StoreBridge()
{
    if (s_self == this) {
        s_self = nullptr;
    }
}

bool StoreBridge::shareFiles() const
{
    return APPLICATION->settings()->get(kShareSetting).toBool();
}

void StoreBridge::setShareFiles(bool on)
{
    if (on == shareFiles()) {
        return;
    }
    APPLICATION->settings()->set(kShareSetting, on);
    emit shareFilesChanged();
    if (on) {
        schedulePass();
    }
}

void StoreBridge::rewatch()
{
    QStringList want;
    for (const auto& ref : allInstances()) {
        for (const auto& d : ref.dirs) {
            if (QFileInfo(d).isDir()) {
                want << d;
            }
        }
    }
    const QStringList have = m_watcher.directories();
    QStringList drop, add;
    for (const auto& d : have) {
        if (!want.contains(d)) {
            drop << d;
        }
    }
    for (const auto& d : want) {
        if (!have.contains(d)) {
            add << d;
        }
    }
    if (!drop.isEmpty()) {
        m_watcher.removePaths(drop);
    }
    if (!add.isEmpty()) {
        m_watcher.addPaths(add);
    }
}

void StoreBridge::schedulePass(const QString& instanceId)
{
    if (instanceId.isEmpty()) {
        m_fullPass = true;
    } else {
        m_pending.insert(instanceId);
    }
    m_debounce.start();
}

void StoreBridge::refresh()
{
    schedulePass();
}

void StoreBridge::runPass()
{
    if (m_passRunning) {
        m_passAgain = true;
        return;
    }
    m_passRunning = true;
    const auto all = allInstances();
    const bool full = m_fullPass;
    const QSet<QString> pending = m_pending;
    m_fullPass = false;
    m_pending.clear();
    const bool share = shareFiles();
    const QString root = storeRoot();

    QPointer<StoreBridge> self(this);
    (void)QtConcurrent::run([self, all, full, pending, share, root] {
        ContentStore::Locker lock;
        auto index = ContentStore::scan(root);
        int linked = 0;
        QStringList roots;
        QList<QStringList> dirs;
        for (const auto& ref : all) {
            roots << ref.root;
            dirs << ref.dirs;
            if (ref.running || (!full && !pending.contains(ref.id))) {
                continue;
            }
            int n = 0;
            ContentStore::process(ref.dirs, index, share, 10, &n);
            linked += n;
        }
        const qint64 freed = ContentStore::gc(index, Snapshots::referenced(roots));
        const auto stats = ContentStore::stats(index, dirs);
        if (linked || freed) {
            qDebug() << "Store: linked" << linked << "files, freed" << freed << "bytes";
        }
        QMetaObject::invokeMethod(
            qApp,
            [self, stats] {
                if (!self) {
                    return;
                }
                self->m_passRunning = false;
                if (self->m_savedBytes != stats.savedBytes || self->m_sharedInstances != stats.instances) {
                    self->m_savedBytes = stats.savedBytes;
                    self->m_sharedInstances = stats.instances;
                    emit self->statsChanged();
                }
                if (self->m_passAgain) {
                    self->m_passAgain = false;
                    self->m_debounce.start();
                }
            },
            Qt::QueuedConnection);
    });
}

void StoreBridge::beforeLaunch(MinecraftInstance* inst)
{
    if (s_self && inst) {
        s_self->snapshotNow(inst);
    }
}

void StoreBridge::snapshotNow(MinecraftInstance* inst)
{
    QString written;
    {
        ContentStore::Locker lock;
        auto index = ContentStore::scan(storeRoot());
        const auto sha = ContentStore::process(Snapshots::contentDirs(inst), index, shareFiles(), 0);
        written = Snapshots::write(inst->instanceRoot(), Snapshots::build(inst, sha));
    }
    if (!written.isEmpty()) {
        m_revision++;
        emit revisionChanged();
    }
    rewatch();  // the launch may have created content folders
    schedulePass();
}

void StoreBridge::loadHistory(const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst) {
        emit historyReady(instanceId, {});
        return;
    }
    const QJsonObject comps = Snapshots::components(inst);
    const auto cats = Snapshots::categories(inst);
    const QString instRoot = inst->instanceRoot();
    const QString root = storeRoot();
    QPointer<StoreBridge> self(this);
    (void)QtConcurrent::run([self, instanceId, comps, cats, instRoot, root] {
        QVariantList out;
        {
            ContentStore::Locker lock;
            auto index = ContentStore::scan(root);
            QStringList dirs;
            for (const auto& c : cats) {
                dirs << c.second;
            }
            const auto sha = ContentStore::process(dirs, index, false, 0);
            QJsonObject now = comps;
            now["files"] = Snapshots::files(cats, sha);
            for (const auto& snap : Snapshots::list(instRoot)) {
                QVariantMap m;
                m["file"] = snap.value("file").toString();
                m["time"] = snap.value("time").toDouble();
                m["reason"] = snap.value("reason").toString();
                m["minecraft"] = snap.value("minecraft").toString();
                const auto loader = snap.value("loader").toObject();
                m["loader"] = loader.value("name").toString();
                m["loaderVersion"] = loader.value("version").toString();
                m["diff"] = Snapshots::diff(snap, now);
                int mods = 0, unavailable = 0;
                const auto files = snap.value("files").toObject();
                for (auto it = files.begin(); it != files.end(); ++it) {
                    for (const auto& v : it.value().toArray()) {
                        const auto e = v.toObject();
                        mods += it.key() == "mods" ? 1 : 0;
                        if (!index.bySha.contains(e.value("sha256").toString()) && e.value("projectId").toString().isEmpty()) {
                            unavailable++;
                        }
                    }
                }
                m["mods"] = mods;
                m["unavailable"] = unavailable;
                out << m;
            }
        }
        QMetaObject::invokeMethod(
            qApp,
            [self, instanceId, out] {
                if (self) {
                    emit self->historyReady(instanceId, out);
                }
            },
            Qt::QueuedConnection);
    });
}

bool StoreBridge::restore(const QString& instanceId, const QString& snapshotFile)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst || inst->isRunning() || !m_restoring.isEmpty()) {
        return false;
    }
    // Only snapshots of this instance.
    if (QFileInfo(snapshotFile).canonicalPath() != QFileInfo(Snapshots::dir(inst->instanceRoot())).canonicalFilePath()) {
        return false;
    }
    const QJsonObject snap = Snapshots::read(snapshotFile);
    if (snap.isEmpty()) {
        return false;
    }
    m_restoring = instanceId;
    emit restoringChanged();

    QVariantMap result{ { "ok", true }, { "restored", 0 }, { "missing", QStringList() }, { "componentsChanged", false } };
    QList<QJsonObject> toDownload;
    QStringList missing;
    int restored = 0;
    {
        ContentStore::Locker lock;
        auto index = ContentStore::scan(storeRoot());
        const auto sha = ContentStore::process(Snapshots::contentDirs(inst), index, false, 0);
        // Keep the current state too, so a restore can itself be undone.
        const QJsonObject current = Snapshots::build(inst, sha);
        Snapshots::write(inst->instanceRoot(), current, "restore");

        // Game version and loader.
        auto* profile = inst->getPackProfile();
        QHash<QString, QString> want;
        QSet<QString> wantCustom;
        for (const auto& v : snap.value("components").toArray()) {
            const auto c = v.toObject();
            want.insert(c.value("uid").toString(), c.value("version").toString());
            if (c.value("custom").toBool()) {
                wantCustom.insert(c.value("uid").toString());
            }
        }
        bool changed = false;
        for (const auto& v : current.value("components").toArray()) {
            const auto c = v.toObject();
            const QString uid = c.value("uid").toString();
            if (!want.contains(uid) && !c.value("custom").toBool() && !c.value("dependencyOnly").toBool()) {
                changed |= profile->remove(uid);
            }
        }
        for (auto it = want.cbegin(); it != want.cend(); ++it) {
            if (wantCustom.contains(it.key()) || it.value().isEmpty()) {
                continue;
            }
            if (profile->getComponentVersion(it.key()) != it.value()) {
                changed |= profile->setComponentVersion(it.key(), it.value());
            }
        }
        if (changed) {
            profile->saveNow();
            profile->resolve(Net::Mode::Online);
        }
        result["componentsChanged"] = changed;

        // Content files.
        const QJsonObject wantFiles = snap.value("files").toObject();
        const QJsonObject curFiles = current.value("files").toObject();
        for (const auto& [cat, dir] : Snapshots::categories(inst)) {
            QHash<QString, QJsonObject> wanted;
            QSet<QString> wantedIndexes;
            for (const auto& v : wantFiles.value(cat).toArray()) {
                const auto e = v.toObject();
                wanted.insert(e.value("name").toString(), e);
                wantedIndexes.insert(e.value("index").toString());
            }
            QHash<QString, QJsonObject> have;
            for (const auto& v : curFiles.value(cat).toArray()) {
                have.insert(v.toObject().value("name").toString(), v.toObject());
            }
            // Files that weren't there: remove (they stay in the store for the "before restoring" snapshot).
            for (const QString& path : ContentStore::contentFiles(dir)) {
                const QString name = QFileInfo(path).fileName();
                if (wanted.contains(name)) {
                    continue;
                }
                const auto cur = have.value(name);
                if (index.bySha.contains(cur.value("sha256").toString())) {
                    QFile::remove(path);
                } else {
                    // Not in the store (another volume): set it aside instead of deleting.
                    const QString aside = QDir(inst->instanceRoot()).filePath(".fashion/removed/" + cat);
                    QDir().mkpath(aside);
                    QFile::remove(QDir(aside).filePath(name));
                    QFile::rename(path, QDir(aside).filePath(name));
                }
                const QString idx = cur.value("index").toString();
                if (!idx.isEmpty() && !wantedIndexes.contains(idx)) {
                    QFile::remove(QDir(dir).filePath(".index/" + idx));
                }
            }
            for (const auto& e : wanted) {
                const QString name = e.value("name").toString();
                if (name.isEmpty() || name.contains('/') || name.contains('\\')) {
                    continue;
                }
                const QString path = QDir(dir).filePath(name);
                const QString key = e.value("sha256").toString();
                if (have.value(name).value("sha256").toString() == key) {
                    writeIndex(dir, e);
                    continue;
                }
                if (ContentStore::materialize(index, key, path)) {
                    writeIndex(dir, e);
                    restored++;
                    continue;
                }
                if (!e.value("projectId").toString().isEmpty()) {
                    QJsonObject d = e;
                    d["dir"] = dir;
                    toDownload << d;
                } else {
                    missing << name;
                }
            }
        }
    }
    result["restored"] = restored;
    result["missing"] = missing;
    if (toDownload.isEmpty()) {
        finishRestore(instanceId, result);
    } else {
        downloadMissing(instanceId, toDownload, result);
    }
    return true;
}

void StoreBridge::downloadMissing(const QString& instanceId, const QList<QJsonObject>& entries, QVariantMap result)
{
    // Step 1: find download links that the metadata didn't keep.
    struct State {
        QList<QJsonObject> entries;
        QList<QByteArray*> lookups;  // per entry, or null
        QStringList urls;
        NetJob::Ptr job;
    };
    auto st = std::make_shared<State>();
    st->entries = entries;
    st->job = makeShared<NetJob>("Store::FindFiles", APPLICATION->network());
    for (const auto& e : entries) {
        st->urls << e.value("url").toString();
        QByteArray* out = nullptr;
        if (st->urls.last().isEmpty() && !e.value("fileId").toString().isEmpty()) {
            QUrl api = e.value("provider").toString() == "modrinth"
                           ? QUrl(BuildConfig.MODRINTH_PROD_URL + "/version/" + e.value("fileId").toString())
                           : QUrl(QString(BuildConfig.FLAME_BASE_URL + "/mods/%1/files/%2")
                                      .arg(e.value("projectId").toString(), e.value("fileId").toString()));
            auto [action, response] = Net::ApiRequest::makeByteArray(api);
            st->job->addNetAction(action);
            out = response;
        }
        st->lookups << out;
    }

    QPointer<StoreBridge> self(this);
    auto download = [self, st, instanceId, result]() mutable {
        if (!self) {
            return;
        }
        for (int i = 0; i < st->entries.size(); i++) {
            if (!st->urls[i].isEmpty() || !st->lookups[i]) {
                continue;
            }
            const auto doc = QJsonDocument::fromJson(*st->lookups[i]).object();
            const QString want = enabledName(st->entries[i].value("name").toString());
            if (doc.contains("files")) {  // Modrinth version
                for (const auto& f : doc.value("files").toArray()) {
                    const auto o = f.toObject();
                    if (o.value("filename").toString() == want || (st->urls[i].isEmpty() && o.value("primary").toBool())) {
                        st->urls[i] = o.value("url").toString();
                    }
                }
            } else {  // CurseForge file
                st->urls[i] = doc.value("data").toObject().value("downloadUrl").toString();
            }
        }
        // Step 2: download into hidden part files, check the hash, then move into place.
        auto job = makeShared<NetJob>("Store::Restore", APPLICATION->network());
        QStringList parts;
        QStringList missing = result.value("missing").toStringList();
        for (int i = 0; i < st->entries.size(); i++) {
            const auto& e = st->entries[i];
            const QString part = QDir(e.value("dir").toString()).filePath("." + e.value("name").toString() + ".part");
            parts << part;
            if (st->urls[i].isEmpty()) {
                missing << e.value("name").toString();
                continue;
            }
            QFile::remove(part);
            job->addNetAction(Net::ApiRequest::makeFile(QUrl(st->urls[i]), part));
        }
        result["missing"] = missing;
        auto done = [self, st, parts, instanceId, result]() mutable {
            if (!self) {
                return;
            }
            QStringList missing = result.value("missing").toStringList();
            int restored = result.value("restored").toInt();
            ContentStore::Locker lock;
            auto index = ContentStore::scan(storeRoot());
            for (int i = 0; i < st->entries.size(); i++) {
                const auto& e = st->entries[i];
                const QString name = e.value("name").toString();
                if (st->urls[i].isEmpty()) {
                    continue;
                }
                const QString part = parts[i];
                if (!QFile::exists(part) || ContentStore::sha256(part) != e.value("sha256").toString()) {
                    QFile::remove(part);
                    missing << name;
                    continue;
                }
                const QString dest = QDir(e.value("dir").toString()).filePath(name);
                QFile::remove(dest);
                if (!QFile::rename(part, dest)) {
                    QFile::remove(part);
                    missing << name;
                    continue;
                }
                ContentStore::ingest(index, dest, e.value("sha256").toString());
                writeIndex(e.value("dir").toString(), e);
                restored++;
            }
            result["missing"] = missing;
            result["restored"] = restored;
            self->finishRestore(instanceId, result);
        };
        if (job->size() == 0) {
            done();
            return;
        }
        QObject::connect(job.get(), &Task::finished, self, [job, done]() mutable { done(); });
        job->start();
    };

    if (st->job->size() == 0) {
        download();
        return;
    }
    connect(st->job.get(), &Task::finished, this, [download]() mutable { download(); });
    st->job->start();
}

void StoreBridge::finishRestore(const QString& instanceId, QVariantMap result)
{
    result["ok"] = result.value("missing").toStringList().isEmpty();
    m_restoring.clear();
    emit restoringChanged();
    m_revision++;
    emit revisionChanged();
    emit restoreFinished(instanceId, result);
    schedulePass();
}
