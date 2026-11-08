// SPDX-License-Identifier: GPL-3.0-only
#include "StorageBridge.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include "Application.h"
#include "InstanceList.h"
#include "meta/Version.h"
#include "minecraft/Component.h"
#include "minecraft/Library.h"
#include "minecraft/LaunchProfile.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VersionFile.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "tasks/Task.h"

#include "../jobs/TaskCenter.h"
#include "../store/ContentStore.h"
#include "../store/Snapshots.h"

namespace {

QString canonicalOrClean(const QString& path)
{
    const QString c = QFileInfo(path).canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : c;
}

/**
 * A Task that runs one function on a worker thread. The function reports progress through the callback it gets
 * (safe from any thread) and its result is delivered on the GUI thread by `done`.
 */
template <typename R>
class WorkerTask : public Task {
   public:
    using Work = std::function<R(const StorageScan::Progress&)>;
    using Done = std::function<void(const R&)>;
    WorkerTask(Work work, Done done) : m_work(std::move(work)), m_done(std::move(done)) {}

   protected:
    void executeTask() override
    {
        QPointer<WorkerTask> self(this);
        auto progress = [self](const QString& status, int cur, int total) {
            QMetaObject::invokeMethod(
                qApp,
                [self, status, cur, total] {
                    if (!self) {
                        return;
                    }
                    if (!status.isEmpty()) {
                        self->setStatus(status);
                    }
                    self->setProgress(cur, total);
                },
                Qt::QueuedConnection);
        };
        auto work = m_work;
        (void)QtConcurrent::run([self, work, progress] {
            R result = work(progress);
            QMetaObject::invokeMethod(
                qApp,
                [self, result] {
                    if (!self) {
                        return;
                    }
                    self->m_done(result);
                    self->emitSucceeded();
                },
                Qt::QueuedConnection);
        });
    }

   private:
    Work m_work;
    Done m_done;
};

}  // namespace

StorageBridge::StorageBridge(QObject* parent) : QObject(parent)
{
    // A game starting or stopping changes the instance's row.
    connect(APPLICATION->instances(), &InstanceList::dataChanged, this, &StorageBridge::blockerChanged);
    connect(TaskCenter::instance(), &TaskCenter::changed, this, &StorageBridge::blockerChanged);
}

QString StorageBridge::blocker() const
{
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); i++) {
        if (list->at(i)->isRunning()) {
            return tr("Close the game first: files of a running game can't be cleaned up.");
        }
    }
    // Our own scan or clean is one of the jobs; any other job may be downloading into these folders.
    const int ours = (m_scanning || m_cleaning) ? 1 : 0;
    if (TaskCenter::instance()->count() > ours) {
        return tr("Wait until other jobs finish: they may be downloading into these folders.");
    }
    return {};
}

StorageScan::Input StorageBridge::gather(int logDays, bool includeScreenshots) const
{
    StorageScan::Input in;
    in.dataRoot = canonicalOrClean(APPLICATION->dataRoot());
    in.storeRoot = canonicalOrClean(ContentStore::root(APPLICATION->dataRoot()));
    in.logDays = logDays;
    in.includeScreenshots = includeScreenshots;

    // Library paths in launch profiles are relative to the working directory, which is the data folder.
    const QDir dataDir(APPLICATION->dataRoot());
    auto keepLib = [&](const LibraryPtr& lib, const RuntimeContext& ctx) {
        if (!lib) {
            return;
        }
        QStringList jar, native, n32, n64;
        lib->getApplicableFiles(ctx, jar, native, n32, n64, {});
        for (const auto& list : { jar, native, n32, n64 }) {
            for (const auto& p : list) {
                in.keepLibraries.insert(canonicalOrClean(dataDir.absoluteFilePath(p)));
            }
        }
    };

    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); i++) {
        auto* inst = dynamic_cast<MinecraftInstance*>(list->at(i));
        if (!inst) {
            continue;
        }
        StorageScan::InstanceInfo info;
        info.id = inst->id();
        info.name = inst->name();
        info.instanceRoot = canonicalOrClean(inst->instanceRoot());
        info.gameRoot = canonicalOrClean(inst->gameRoot());
        for (const auto& d : Snapshots::contentDirs(inst)) {
            info.contentDirs << canonicalOrClean(d);
        }
        info.running = inst->isRunning();
        in.instances << info;

        auto* profile = inst->getPackProfile();
        if (profile->getComponentVersion("net.minecraft").isEmpty()) {
            if (auto res = profile->reload(Net::Mode::Offline); !res) {
                qWarning() << "Storage: could not load components of" << inst->name() << res.error();
            }
        }
        // Version files of components are read lazily (at launch). Read the ones that are on disk now, without
        // network: a load task in offline mode with a local file finishes synchronously.
        bool loadedAny = false;
        for (int c = 0; c < profile->rowCount(); c++) {
            auto comp = profile->getComponent(size_t(c));
            auto meta = comp ? comp->getMeta() : nullptr;
            if (meta && !comp->getVersionFile() && QFile::exists(dataDir.absoluteFilePath("meta/" + meta->localFilename()))) {
                auto load = meta->loadTask(Net::Mode::Offline);
                load->start();
                loadedAny = loadedAny || comp->getVersionFile();
            }
        }
        if (loadedAny) {
            profile->invalidateLaunchProfile();
        }
        // Every component must have its version file: otherwise its libraries are unknown and could look unused.
        bool ok = profile->rowCount() > 0;
        for (int c = 0; ok && c < profile->rowCount(); c++) {
            auto comp = profile->getComponent(size_t(c));
            ok = comp && comp->getVersionFile() && comp->getProblemSeverity() != ProblemSeverity::Error;
            if (!ok) {
                qDebug() << "Storage:" << inst->name() << "can't be checked, component" << (comp ? comp->getID() : QString("?"))
                         << "is not downloaded";
            }
        }
        auto launch = ok ? profile->getProfile() : nullptr;
        if (!launch) {
            // Developer aid for testing on data with half-downloaded instances; never set for users.
            if (qEnvironmentVariableIntValue("FFPRISM_STORAGE_IGNORE_BROKEN")) {
                continue;
            }
            if (in.profilesOk) {
                in.profilesOk = false;
                in.profilesProblem = inst->name();
            }
            continue;
        }
        const auto ctx = inst->runtimeContext();
        for (const auto& lib : launch->getLibraries()) {
            keepLib(lib, ctx);
        }
        for (const auto& lib : launch->getNativeLibraries()) {
            keepLib(lib, ctx);
        }
        for (const auto& lib : launch->getJarMods()) {
            keepLib(lib, ctx);
        }
        for (const auto& lib : launch->getMavenFiles()) {
            keepLib(lib, ctx);
        }
        for (const auto& agent : launch->getAgents()) {
            keepLib(agent.library, ctx);
        }
        keepLib(launch->getMainJar(), ctx);
        if (auto assets = launch->getMinecraftAssets()) {
            in.assetIds.insert(assets->id);
        }
    }
    return in;
}

void StorageBridge::scan(int logDays, bool includeScreenshots)
{
    if (m_scanning || m_cleaning) {
        return;
    }
    m_logDays = logDays;
    m_screenshots = includeScreenshots;
    const auto in = gather(logDays, includeScreenshots);
    m_scanning = true;
    emit stateChanged();

    QPointer<StorageBridge> self(this);
    auto task = makeShared<WorkerTask<StorageScan::Result>>(
        [in](const StorageScan::Progress& p) { return StorageScan::scan(in, p); },
        [self](const StorageScan::Result& r) {
            if (!self) {
                return;
            }
            self->m_last = r;
            QVariantList usage, cats;
            for (const auto& u : r.usage) {
                usage << QVariantMap{ { "id", u.id }, { "bytes", double(u.bytes) } };
            }
            for (const auto& c : r.categories) {
                cats << QVariantMap{ { "id", c.id },             { "bytes", double(c.bytes) },
                                     { "count", c.files.size() }, { "available", c.available },
                                     { "note", c.note },          { "lossless", c.lossless } };
            }
            self->m_report = QVariantMap{ { "usage", usage }, { "total", double(r.total) }, { "categories", cats },
                                          { "dataRoot", r.dataRoot } };
            self->m_scanning = false;
            emit self->stateChanged();
            emit self->reportChanged();
            emit self->blockerChanged();
            self->debugHooks();
        });
    TaskCenter::instance()->track(task, tr("Measuring disk space"), "storage", {}, "trash");
    task->start();
}

void StorageBridge::clean(const QStringList& categoryIds)
{
    if (m_scanning || m_cleaning || categoryIds.isEmpty() || !blocker().isEmpty()) {
        return;
    }
    m_cleaning = true;
    emit stateChanged();
    const auto last = m_last;
    QPointer<StorageBridge> self(this);
    auto task = makeShared<WorkerTask<StorageScan::CleanOutcome>>(
        [last, categoryIds](const StorageScan::Progress& p) { return StorageScan::clean(last, categoryIds, p); },
        [self](const StorageScan::CleanOutcome& out) {
            if (!self) {
                return;
            }
            self->m_cleaning = false;
            emit self->stateChanged();
            emit self->cleaned(double(out.freed), out.failed);
            self->scan(self->m_logDays, self->m_screenshots);
        });
    TaskCenter::instance()->track(task, tr("Freeing up disk space"), "storage", {}, "trash");
    task->start();
}

// Developer aids, only with environment variables: FFPRISM_STORAGE_LIST=<file> writes every candidate of each scan
// ("<category>\t<bytes>\t<path>"), FFPRISM_STORAGE_CLEAN=<id,id> cleans those categories once after the first scan.
// FFPRISM_STORAGE_IGNORE_BROKEN=1 skips instances whose profile is not downloaded (testing only).
void StorageBridge::debugHooks()
{
    if (const QString out = qEnvironmentVariable("FFPRISM_STORAGE_LIST"); !out.isEmpty()) {
        QFile f(out);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            for (const auto& c : m_last.categories) {
                ts << "#" << c.id << "\t" << c.bytes << "\t" << c.files.size() << "\t" << c.note << "\n";
                for (const auto& file : c.files) {
                    ts << c.id << "\t" << file.size << "\t" << file.path << "\n";
                }
            }
        }
    }
    static bool cleanedOnce = false;
    if (const QString ids = qEnvironmentVariable("FFPRISM_STORAGE_CLEAN"); !ids.isEmpty() && !cleanedOnce) {
        cleanedOnce = true;
        QTimer::singleShot(0, this, [this, ids] { clean(ids.split(',', Qt::SkipEmptyParts)); });
    }
}

QVariantList StorageBridge::files(const QString& categoryId, int limit) const
{
    QVariantList out;
    for (const auto& c : m_last.categories) {
        if (c.id != categoryId) {
            continue;
        }
        for (const auto& f : c.files) {
            if (out.size() >= limit) {
                break;
            }
            QString shown = f.path;
            if (shown.startsWith(m_last.dataRoot + '/')) {
                shown = shown.mid(m_last.dataRoot.size() + 1);
            }
            out << QVariantMap{ { "path", f.path }, { "shown", shown }, { "bytes", double(f.size) } };
        }
    }
    return out;
}

void StorageBridge::reveal(const QString& path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}
