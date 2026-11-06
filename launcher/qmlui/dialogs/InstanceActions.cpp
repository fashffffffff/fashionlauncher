// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceActions.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "Application.h"
#include "FileIgnoreProxy.h"
#include "FileSystem.h"
#include "InstanceCopyPrefs.h"
#include "InstanceCopyTask.h"
#include "InstanceList.h"
#include "MMCZip.h"
#include "archive/ExportToZipTask.h"
#include "icons/IconList.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VersionFile.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "modplatform/flame/FlamePackExportTask.h"
#include "modplatform/modrinth/ModrinthPackExportTask.h"
#include "settings/SettingsObject.h"

// Defined in ui/dialogs/ExportInstanceDialog.cpp: copies a custom icon into the instance so the zip carries it.
void SaveIcon(BaseInstance* m_instance);

namespace {
BaseInstance* findInstance(const QString& id)
{
    return APPLICATION->instances()->getInstanceById(id);
}

/// The same exclusions the classic export dialogs set up, with .packignore loaded.
FileIgnoreProxy* makeIgnoreProxy(BaseInstance* inst, const QString& format, QObject* parent)
{
    auto* proxy = new FileIgnoreProxy(inst->instanceRoot(), parent);
    const auto prefix = QDir(inst->instanceRoot()).relativeFilePath(inst->gameRoot());
    for (auto path : { "logs", "crash-reports", ".cache", ".fabric", ".quilt" }) {
        proxy->ignoreFilesWithPath().insert(FS::PathCombine(prefix, path));
    }
    proxy->ignoreFilesWithName().append({ ".DS_Store", "thumbs.db", "Thumbs.db" });
    if (format != "zip") {
        proxy->ignoreFilesWithSuffix().append(".pw.toml");
        if (auto* mc = dynamic_cast<MinecraftInstance*>(inst)) {
            const QDir root(inst->instanceRoot());
            for (auto* list : mc->resourceLists()) {
                if (list && list->indexDir().exists() && list->dir() != list->indexDir()) {
                    proxy->ignoreFilesWithPath().insert(root.relativeFilePath(list->indexDir().absolutePath()));
                }
            }
        }
    }
    proxy->loadBlockedPathsFromFile(FS::PathCombine(inst->instanceRoot(), ".packignore"));
    return proxy;
}

QString localPath(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}
}  // namespace

InstanceActions::InstanceActions(QObject* parent) : QObject(parent) {}

QStringList InstanceActions::groups() const
{
    auto list = APPLICATION->instances()->getGroups();
    list.removeAll(QString());
    list.sort(Qt::CaseInsensitive);
    return list;
}

QVariantMap InstanceActions::copyDefaults(const QString& id) const
{
    auto* inst = findInstance(id);
    if (!inst) {
        return {};
    }
    return { { "name", inst->name() }, { "group", APPLICATION->instances()->getInstanceGroup(id) }, { "icon", inst->iconKey() } };
}

bool InstanceActions::copy(const QString& id, const QVariantMap& o)
{
    auto* inst = findInstance(id);
    if (!inst || m_task) {
        return false;
    }
    InstanceCopyPrefs prefs;
    prefs.enableCopySaves(o.value("saves", true).toBool());
    prefs.enableKeepPlaytime(o.value("keepPlaytime", true).toBool());
    prefs.enableCopyGameOptions(o.value("options", true).toBool());
    prefs.enableCopyResourcePacks(o.value("resourcePacks", true).toBool());
    prefs.enableCopyShaderPacks(o.value("shaderPacks", true).toBool());
    prefs.enableCopyServers(o.value("servers", true).toBool());
    prefs.enableCopyMods(o.value("mods", true).toBool());
    prefs.enableCopyScreenshots(o.value("screenshots", true).toBool());
    // Links save disk space: the copy shares the original's files instead of duplicating them.
    if (o.value("links", false).toBool()) {
        prefs.enableUseSymLinks(true);
        prefs.enableLinkRecursively(true);
        prefs.enableDontLinkSaves(true);
    }
    auto* copyTask = new InstanceCopyTask(inst, prefs);
    const QString name = o.value("name").toString().trimmed();
    copyTask->setName(name.isEmpty() ? inst->name() : name);
    copyTask->setGroup(o.value("group").toString().trimmed());
    copyTask->setIcon(o.value("icon", inst->iconKey()).toString());
    return runJob(APPLICATION->instances()->wrapInstanceTask(copyTask), tr("Copying %1").arg(inst->name()),
                  tr("Copied as “%1”.").arg(name.isEmpty() ? inst->name() : name));
}

QString InstanceActions::deleteProblem(const QString& id) const
{
    auto* inst = findInstance(id);
    if (!inst) {
        return tr("This instance no longer exists.");
    }
    if (inst->isRunning()) {
        return tr("The game is running. Close it before deleting this instance.");
    }
    return {};
}

QStringList InstanceActions::linkedInstances(const QString& id) const
{
    QStringList names;
    for (const auto& other : APPLICATION->instances()->getLinkedInstancesById(id)) {
        if (auto* inst = findInstance(other)) {
            names << inst->name();
        }
    }
    return names;
}

bool InstanceActions::remove(const QString& id)
{
    if (!deleteProblem(id).isEmpty()) {
        return false;
    }
    APPLICATION->settings()->set("SelectedInstance", QString());
    if (APPLICATION->instances()->trashInstance(id)) {
        return true;
    }
    APPLICATION->instances()->deleteInstance(id);
    return false;
}

QString InstanceActions::iconKey(const QString& id) const
{
    auto* inst = findInstance(id);
    return inst ? inst->iconKey() : QString();
}

void InstanceActions::setIcon(const QString& id, const QString& key)
{
    if (auto* inst = findInstance(id); inst && !key.isEmpty()) {
        inst->setIconKey(key);
    }
}

QVariantMap InstanceActions::exportDefaults(const QString& id, const QString& format) const
{
    auto* inst = findInstance(id);
    if (!inst) {
        return {};
    }
    auto s = inst->settings();
    QVariantMap out{
        { "name", s->get("ExportName").toString() },
        { "version", s->get("ExportVersion").toString() },
        { "author", s->get("ExportAuthor").toString() },
        { "summary", s->get("ExportSummary").toString() },
        { "optionalFiles", s->get("ExportOptionalFiles").toBool() },
        { "recommendedRam", s->get("ExportRecommendedRAM").toInt() },
        { "problem", QString() },
    };
    auto* mc = dynamic_cast<MinecraftInstance*>(inst);
    if (format != "zip" && !mc) {
        out["problem"] = tr("Only Minecraft instances can be exported as modpacks.");
    } else if (format == "curseforge") {
        if (auto cmp = mc->getPackProfile()->getComponent("net.minecraft");
            cmp && cmp->getVersionFile() && cmp->getVersionFile()->type == "snapshot") {
            out["problem"] = tr("Snapshots are currently not supported by CurseForge modpacks.");
        }
    }
    return out;
}

QVariantList InstanceActions::exportEntries(const QString& id, const QString& format) const
{
    auto* inst = findInstance(id);
    if (!inst) {
        return {};
    }
    QObject holder;
    auto* proxy = makeIgnoreProxy(inst, format, &holder);
    // What players choose between lives in the game folder; instance files around it always go along.
    const QDir dir(inst->gameRoot());
    const QDir root(inst->instanceRoot());
    QVariantList out;
    const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::DirsFirst | QDir::IgnoreCase);
    for (const auto& fi : entries) {
        const QString rel = root.relativeFilePath(fi.absoluteFilePath());
        if (proxy->ignoreFilesWithName().contains(fi.fileName()) || proxy->ignoreFilesWithPath().covers(rel) || rel == ".packignore") {
            continue;
        }
        bool hidden = false;
        for (const auto& suffix : proxy->ignoreFilesWithSuffix()) {
            hidden |= fi.fileName().endsWith(suffix);
        }
        if (hidden) {
            continue;
        }
        out.append(QVariantMap{
            { "path", rel }, { "name", fi.fileName() }, { "isDir", fi.isDir() }, { "included", !proxy->blockedPaths().covers(rel) } });
    }
    return out;
}

QString InstanceActions::exportFileName(const QString& id, const QString& format, const QString& name) const
{
    auto* inst = findInstance(id);
    const QString base = FS::RemoveInvalidFilenameChars(name.trimmed().isEmpty() ? (inst ? inst->name() : "instance") : name.trimmed());
    return FS::PathCombine(QDir::homePath(), base + (format == "modrinth" ? ".mrpack" : ".zip"));
}

bool InstanceActions::exportTo(const QString& id, const QString& format, const QString& outputUrl, const QVariantMap& o,
                               const QStringList& excluded)
{
    auto* inst = findInstance(id);
    if (!inst || m_task) {
        return false;
    }
    QString output = localPath(outputUrl);
    if (output.isEmpty()) {
        return false;
    }
    if (format == "modrinth" && !output.endsWith(".mrpack") && !output.endsWith(".zip")) {
        output += ".mrpack";
    } else if (format != "modrinth" && !output.endsWith(".zip")) {
        output += ".zip";
    }

    // Remember the choice in .packignore, like the classic dialogs: only entries listed here change.
    auto* proxy = makeIgnoreProxy(inst, format, nullptr);
    for (const auto& entry : exportEntries(id, format)) {
        const QString path = entry.toMap().value("path").toString();
        const bool wantExcluded = excluded.contains(path);
        const bool isExcluded = proxy->blockedPaths().covers(path);
        if (wantExcluded && !isExcluded) {
            proxy->blockedPaths().insert(path);
        } else if (!wantExcluded && isExcluded) {
            proxy->blockedPaths().remove(path);
        }
    }
    proxy->saveBlockedPathsToFile(FS::PathCombine(inst->instanceRoot(), ".packignore"));
    auto filter = std::bind(&FileIgnoreProxy::filterFile, proxy, std::placeholders::_1);

    const QString name = o.value("name").toString().trimmed().isEmpty() ? inst->name() : o.value("name").toString().trimmed();
    Task* task = nullptr;
    if (format == "zip") {
        SaveIcon(inst);
        QFileInfoList files;
        if (!MMCZip::collectFileListRecursively(inst->instanceRoot(), nullptr, &files, filter)) {
            delete proxy;
            emit jobFinished(false, tr("Unable to export instance"));
            return false;
        }
        task = new MMCZip::ExportToZipTask(output, inst->instanceRoot(), files, "", true);
    } else {
        auto* mc = dynamic_cast<MinecraftInstance*>(inst);
        if (!mc) {
            delete proxy;
            return false;
        }
        auto s = inst->settings();
        s->set("ExportName", o.value("name").toString());
        s->set("ExportVersion", o.value("version").toString());
        s->set("ExportOptionalFiles", o.value("optionalFiles").toBool());
        if (format == "modrinth") {
            s->set("ExportSummary", o.value("summary").toString());
            task = new ModrinthPackExportTask(name, o.value("version").toString(), o.value("summary").toString(),
                                              o.value("optionalFiles").toBool(), mc, output, filter);
        } else {
            s->set("ExportAuthor", o.value("author").toString());
            const int ram = o.value("recommendedRam").toInt();
            if (ram > 0) {
                s->set("ExportRecommendedRAM", ram);
            } else {
                s->reset("ExportRecommendedRAM");
            }
            FlamePackExportOptions options{};
            options.name = name;
            options.version = o.value("version").toString();
            options.author = o.value("author").toString();
            options.optionalFiles = o.value("optionalFiles").toBool();
            options.instance = mc;
            options.output = output;
            options.filter = filter;
            options.recommendedRAM = ram;
            task = new FlamePackExportTask(std::move(options));
        }
    }
    proxy->setParent(task);  // the filter must live as long as the task
    return runJob(task, tr("Exporting %1").arg(inst->name()), tr("Exported to %1").arg(output));
}

bool InstanceActions::runJob(Task* task, const QString& title, const QString& doneMessage)
{
    if (!task) {
        return false;
    }
    m_task = task;
    m_job = { { "active", true }, { "title", title }, { "status", QString() }, { "progress", -1 } };
    emit jobChanged();
    connect(task, &Task::status, this, [this](const QString& status) {
        m_job["status"] = status;
        emit jobChanged();
    });
    connect(task, &Task::progress, this, [this](qint64 current, qint64 total) {
        m_job["progress"] = total > 0 ? double(current) / double(total) : -1.0;
        emit jobChanged();
    });
    connect(task, &Task::finished, this, [this, task, doneMessage] {
        const bool ok = task->wasSuccessful();
        QString message = ok ? doneMessage : task->failReason();
        if (task->getState() == Task::State::AbortedByUser) {
            message = tr("Cancelled.");
        } else if (ok && !task->warnings().isEmpty()) {
            message += "\n" + task->warnings().join('\n');
        }
        m_job.clear();
        m_task.clear();
        emit jobChanged();
        emit jobFinished(ok, message);
        task->deleteLater();
    });
    task->start();
    return true;
}

void InstanceActions::cancelJob()
{
    if (m_task && m_task->canAbort()) {
        m_task->abort();
    }
}
