// SPDX-License-Identifier: GPL-3.0-only
#include "ControlServer.h"

#include <QCoreApplication>
#include <algorithm>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include "Application.h"
#include "BuildConfig.h"
#include "ControlSpec.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "LaunchPrompts.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "minecraft/World.h"
#include "minecraft/WorldList.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "qmlui/dialogs/GameLog.h"
#include "qmlui/doctor/CrashDoctor.h"
#include "qmlui/jobs/TaskCenter.h"
#include "qmlui/resources/ResourceCommon.h"
#include "qmlui/resources/SearchModel.h"
#include "qmlui/store/StoreBridge.h"
#include "settings/SettingsObject.h"

namespace {

struct LoaderUid {
    const char* key;
    const char* uid;
    const char* name;
};
const LoaderUid kLoaders[] = {
    { "fabric", "net.fabricmc.fabric-loader", "Fabric" },
    { "quilt", "org.quiltmc.quilt-loader", "Quilt" },
    { "forge", "net.minecraftforge", "Forge" },
    { "neoforge", "net.neoforged", "NeoForge" },
};

const LoaderUid* loaderFor(const QString& key)
{
    for (const auto& l : kLoaders) {
        if (key == QLatin1String(l.key)) {
            return &l;
        }
    }
    return nullptr;
}

/// An instance by id, or by exact (then case-insensitive) name
MinecraftInstance* resolveInstance(const QString& ref)
{
    if (ref.isEmpty()) {
        return nullptr;
    }
    if (auto* inst = QmlResources::findInstance(ref)) {
        return inst;
    }
    auto* list = APPLICATION->instances();
    MinecraftInstance* loose = nullptr;
    for (int i = 0; i < list->count(); ++i) {
        auto* inst = dynamic_cast<MinecraftInstance*>(list->at(i));
        if (!inst) {
            continue;
        }
        if (inst->name() == ref) {
            return inst;
        }
        if (!loose && inst->name().compare(ref, Qt::CaseInsensitive) == 0) {
            loose = inst;
        }
    }
    return loose;
}

ResourceFolderModel* folderFor(MinecraftInstance* inst, const QString& kind)
{
    if (kind == "resourcepacks") {
        return inst->resourcePackList();
    }
    if (kind == "shaderpacks") {
        return inst->shaderPackList();
    }
    return inst->loaderModList();
}

QJsonObject instanceSummary(MinecraftInstance* inst)
{
    return QJsonObject{
        { "id", inst->id() },
        { "name", inst->name() },
        { "minecraft", QmlResources::gameVersion(inst) },
        { "loader", QmlResources::loaderNames(inst) },
        { "group", APPLICATION->instances()->getInstanceGroup(inst->id()) },
        { "running", inst->isRunning() },
        { "play_time_minutes", static_cast<qint64>(inst->totalTimePlayed() / 60) },
    };
}

/// Runs `then` once the folder model has rescanned (or after a short wait if no scan comes)
void afterScan(ResourceFolderModel* folder, QObject* context, std::function<void()> then)
{
    auto done = std::make_shared<bool>(false);
    auto finish = [done, then] {
        if (!*done) {
            *done = true;
            then();
        }
    };
    QObject::connect(folder, &ResourceFolderModel::updateFinished, context, finish, Qt::SingleShotConnection);
    if (!folder->update() && !folder->empty()) {
        QTimer::singleShot(0, context, finish);  // already up to date
    }
    QTimer::singleShot(8000, context, finish);  // never hang on a scan that doesn't report back
}

/// Runs `then` once the metadata list is loaded (starting the load if needed), or after `timeoutMs`
void whenLoaded(const Meta::VersionList::Ptr& list, QObject* context, std::function<void()> then, int timeoutMs = 60000)
{
    if (list->isLoaded()) {
        then();
        return;
    }
    // getLoadTask() makes a new task each time: keep it alive (captured below) until we are done
    auto task = list->getLoadTask();
    if (task && !task->isRunning() && !task->isFinished()) {
        task->start();
    }
    // Poll instead of waiting for a signal: another load of this list may be the one that finishes
    auto* timer = new QTimer(context);
    auto waited = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, context, [timer, list, task, then, waited, timeoutMs] {
        *waited += 250;
        if (list->isLoaded() || *waited >= timeoutMs) {
            timer->stop();
            timer->deleteLater();
            then();
        }
    });
    timer->start(250);
}

}  // namespace

ControlServer::ControlServer(QObject* parent) : QObject(parent) {}

ControlServer::~ControlServer()
{
    removeRegistry();
}

QString ControlServer::socketName(const QString& dataPath)
{
    const QString canon = QDir(dataPath).canonicalPath().isEmpty() ? QDir(dataPath).absolutePath() : QDir(dataPath).canonicalPath();
    return "fashionlauncher-ctl-" + QCryptographicHash::hash(canon.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
}

bool ControlServer::start()
{
    const QString name = socketName(APPLICATION->dataRoot());
    QLocalServer::removeServer(name);  // a stale socket from a crashed run
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);  // only this OS user
    if (!m_server->listen(name)) {
        qWarning() << "[Control] could not listen on" << name << m_server->errorString();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &ControlServer::onConnection);
    writeRegistry();
    connect(qApp, &QCoreApplication::aboutToQuit, this, &ControlServer::removeRegistry);
    qDebug() << "[Control] listening on" << m_server->fullServerName();
    return true;
}

void ControlServer::writeRegistry()
{
    QDir().mkpath(ControlSpec::registryDir());
    m_registryFile = QDir(ControlSpec::registryDir()).filePath(socketName(APPLICATION->dataRoot()) + ".json");
    const QJsonObject entry{
        { "socket", m_server->serverName() },
        { "pid", QCoreApplication::applicationPid() },
        { "data", QDir(APPLICATION->dataRoot()).absolutePath() },
        { "custom_data", APPLICATION->dataDirOverridden() },
        { "binary", QCoreApplication::applicationFilePath() },
        { "version", BuildConfig.printableVersionString() },
        { "protocol", ControlSpec::protocolVersion },
    };
    QFile f(m_registryFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
    }
}

void ControlServer::removeRegistry()
{
    if (!m_registryFile.isEmpty()) {
        QFile::remove(m_registryFile);
        m_registryFile.clear();
    }
}

void ControlServer::onConnection()
{
    while (auto* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine()) {
                onLine(socket, socket->readLine().trimmed());
            }
        });
    }
}

void ControlServer::onLine(QLocalSocket* socket, const QByteArray& line)
{
    if (line.isEmpty()) {
        return;
    }
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(line, &err);
    const QJsonObject req = doc.object();
    const QJsonValue id = req.value("id");
    QPointer<QLocalSocket> guard(socket);
    auto reply = [guard, id](const QJsonValue& result, const QString& error) {
        if (!guard) {
            return;
        }
        QJsonObject out{ { "id", id }, { "ok", error.isEmpty() } };
        if (error.isEmpty()) {
            out["result"] = result;
        } else {
            out["error"] = error;
        }
        guard->write(QJsonDocument(out).toJson(QJsonDocument::Compact) + '\n');
        guard->flush();
    };
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        reply({}, "Invalid request: expected one JSON object per line");
        return;
    }
    const QString cmd = req.value("cmd").toString();
    if (cmd == "hello") {
        reply(QJsonObject{ { "protocol", ControlSpec::protocolVersion }, { "version", BuildConfig.printableVersionString() } }, {});
        return;
    }
    if (!ControlSpec::has(cmd)) {
        reply({}, QString("Unknown command \"%1\"").arg(cmd));
        return;
    }
    qDebug() << "[Control]" << cmd;
    dispatch(cmd, req.value("args").toObject(), reply);
}

void ControlServer::dispatch(const QString& cmd, const QJsonObject& args, const Reply& reply)
{
    // commands without an instance
    if (cmd == "status") {
        return status(reply);
    }
    if (cmd == "list_instances") {
        return listInstances(reply);
    }
    if (cmd == "list_minecraft_versions") {
        return listVersions(args, reply);
    }
    if (cmd == "create_instance") {
        return createInstance(args, reply);
    }
    if (cmd == "list_accounts") {
        return listAccounts(reply);
    }
    if (cmd == "set_default_account") {
        return setDefaultAccount(args, reply);
    }

    auto* inst = resolveInstance(args.value("instance").toString());
    if (!inst) {
        return reply({}, QString("No instance \"%1\". Use list_instances to see ids and names.").arg(args.value("instance").toString()));
    }
    const QString kind = args.value("kind").toString("mods");
    if (cmd == "get_instance") {
        return getInstance(inst, reply);
    }
    if (cmd == "rename_instance") {
        const QString name = args.value("name").toString().trimmed();
        if (name.isEmpty()) {
            return reply({}, "The new name is empty");
        }
        inst->setName(name);
        return reply(instanceSummary(inst), {});
    }
    if (cmd == "delete_instance") {
        return deleteInstance(inst, reply);
    }
    if (cmd == "set_instance_settings") {
        return setInstanceSettings(inst, args, reply);
    }
    if (cmd == "list_mods") {
        return listMods(inst, kind, reply);
    }
    if (cmd == "search_mods") {
        return searchMods(inst, args, reply);
    }
    if (cmd == "install_mods") {
        return installMods(inst, args, reply);
    }
    if (cmd == "set_mod_enabled") {
        return changeMod(inst, args, false, reply);
    }
    if (cmd == "remove_mod") {
        return changeMod(inst, args, true, reply);
    }
    if (cmd == "list_worlds") {
        return listWorlds(inst, reply);
    }
    if (cmd == "launch") {
        return launch(inst, args, reply);
    }
    if (cmd == "stop") {
        if (!inst->isRunning()) {
            return reply(QJsonObject{ { "stopped", false }, { "reason", "not running" } }, {});
        }
        const bool ok = APPLICATION->kill(inst, false);
        return reply(QJsonObject{ { "stopped", ok } }, ok ? QString() : "The game could not be stopped right now");
    }
    if (cmd == "get_log") {
        return getLog(inst, args, reply);
    }
    if (cmd == "diagnose_crash") {
        return diagnose(inst, reply);
    }
    reply({}, QString("\"%1\" is not available").arg(cmd));
}

// ---- overview ----------------------------------------------------------------------------------

void ControlServer::status(const Reply& reply)
{
    QJsonArray running;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        if (auto inst = list->at(i); inst && inst->isRunning()) {
            running.append(inst->id());
        }
    }
    QJsonArray jobs;
    auto* tc = TaskCenter::instance();
    for (int row = 0; row < tc->rowCount(); ++row) {
        const auto idx = tc->index(row);
        jobs.append(QJsonObject{
            { "title", tc->data(idx, TaskCenter::TitleRole).toString() },
            { "status", tc->data(idx, TaskCenter::StatusRole).toString() },
            { "progress", tc->data(idx, TaskCenter::ProgressRole).toDouble() },
            { "instance", tc->data(idx, TaskCenter::InstanceRole).toString() },
        });
    }
    auto acc = APPLICATION->accounts()->defaultAccount();
    reply(QJsonObject{
              { "launcher", BuildConfig.LAUNCHER_DISPLAYNAME },
              { "version", BuildConfig.printableVersionString() },
              { "data_folder", QDir(APPLICATION->dataRoot()).absolutePath() },
              { "default_account", acc ? acc->profileName() : QString() },
              { "running_instances", running },
              { "jobs", jobs },
          },
          {});
}

void ControlServer::listInstances(const Reply& reply)
{
    QJsonArray out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        if (auto* inst = dynamic_cast<MinecraftInstance*>(list->at(i))) {
            out.append(instanceSummary(inst));
        }
    }
    reply(out, {});
}

void ControlServer::getInstance(MinecraftInstance* inst, const Reply& reply)
{
    QJsonObject o = instanceSummary(inst);
    auto s = inst->settings();
    const bool ownMemory = s->get("OverrideMemory").toBool();
    o["max_memory_mb"] = (ownMemory ? s : APPLICATION->settings())->get("MaxMemAlloc").toInt();
    o["min_memory_mb"] = (ownMemory ? s : APPLICATION->settings())->get("MinMemAlloc").toInt();
    o["java_args"] = s->get("OverrideJavaArgs").toBool() ? s->get("JvmArgs").toString() : QString();
    o["java_path"] = s->get("OverrideJavaLocation").toBool() ? s->get("JavaPath").toString() : QString();
    o["folder"] = QDir(inst->gameRoot()).absolutePath();
    o["last_played"] = inst->lastLaunch() > 0 ? QDateTime::fromMSecsSinceEpoch(inst->lastLaunch()).toString(Qt::ISODate) : QString();
    if (auto* mods = inst->loaderModList()) {
        o["mods"] = static_cast<int>(mods->size());
    }
    if (auto* rp = inst->resourcePackList()) {
        o["resource_packs"] = static_cast<int>(rp->size());
    }
    if (auto* sp = inst->shaderPackList()) {
        o["shader_packs"] = static_cast<int>(sp->size());
    }
    if (auto* worlds = inst->worldList()) {
        o["worlds"] = static_cast<int>(worlds->size());
    }
    reply(o, {});
}

// ---- versions and creation ---------------------------------------------------------------------

void ControlServer::listVersions(const QJsonObject& args, const Reply& reply)
{
    const bool snapshots = args.value("include_snapshots").toBool(false);
    const int limit = qBound(1, args.value("limit").toInt(40), 500);
    auto list = APPLICATION->metadataIndex()->get("net.minecraft");
    auto respond = [list, snapshots, limit, reply] {
        auto versions = list->versions();
        std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) { return a->time() > b->time(); });
        QJsonArray out;
        for (const auto& v : versions) {
            if (!snapshots && v->type() != "release") {
                continue;
            }
            out.append(QJsonObject{ { "version", v->version() }, { "type", v->type() }, { "released", v->time().date().toString(Qt::ISODate) } });
            if (out.size() >= limit) {
                break;
            }
        }
        reply(out, out.isEmpty() ? QString("The version list could not be loaded") : QString());
    };
    whenLoaded(list, this, respond);
}

void ControlServer::createInstance(const QJsonObject& args, const Reply& reply)
{
    const QString mcVersion = args.value("minecraft_version").toString().trimmed();
    const QString loaderKey = args.value("loader").toString("none");
    const auto* loader = loaderFor(loaderKey);
    if (mcVersion.isEmpty()) {
        return reply({}, "minecraft_version is required");
    }
    if (loaderKey != "none" && !loader) {
        return reply({}, QString("Unknown loader \"%1\"").arg(loaderKey));
    }
    const QString wantedLoaderVersion = args.value("loader_version").toString().trimmed();
    const QString name = args.value("name").toString().trimmed();
    const QString group = args.value("group").toString().trimmed();

    // Load the Minecraft list (and the loader's), then pick the versions and run the creation task.
    auto mcList = APPLICATION->metadataIndex()->get("net.minecraft");
    auto loaderList = loader ? APPLICATION->metadataIndex()->get(loader->uid) : Meta::VersionList::Ptr();

    auto proceed = [this, mcList, loaderList, loader, mcVersion, wantedLoaderVersion, name, group, reply] {
        auto mc = mcList->getVersion(mcVersion);
        if (!mc || !mcList->versions().contains(mc)) {
            return reply({}, QString("Minecraft %1 is not in the version list").arg(mcVersion));
        }
        InstanceTask* task = nullptr;
        QString suggested = mcVersion;
        if (loader) {
            Meta::Version::Ptr lv;
            if (!wantedLoaderVersion.isEmpty()) {
                lv = loaderList->getVersion(wantedLoaderVersion);
                if (!loaderList->versions().contains(lv)) {
                    lv.reset();
                }
            } else {
                lv = loaderList->getRecommendedForParent("net.minecraft", mcVersion);
                bool tiedToMinecraft = false;  // Forge/NeoForge builds name their Minecraft; Fabric/Quilt fit any
                auto versions = loaderList->versions();
                std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) { return a->time() > b->time(); });
                if (!lv) {
                    for (const auto& v : versions) {
                        for (const auto& req : v->requiredSet()) {
                            if (req.uid == "net.minecraft") {
                                tiedToMinecraft = true;
                                if (req.equalsVersion == mcVersion) {
                                    lv = v;
                                }
                            }
                        }
                        if (lv) {
                            break;
                        }
                    }
                }
                if (!lv && !tiedToMinecraft) {
                    // the recommended build, else the newest stable one
                    lv = std::dynamic_pointer_cast<Meta::Version>(loaderList->getRecommended());
                    for (const auto& v : versions) {
                        if (lv) {
                            break;
                        }
                        if (v->type().isEmpty() || v->type() == "release") {
                            lv = v;
                        }
                    }
                }
            }
            if (!lv) {
                return reply({}, QString("No %1 version for Minecraft %2%3")
                                     .arg(loader->name, mcVersion, wantedLoaderVersion.isEmpty() ? QString() : " (" + wantedLoaderVersion + ")"));
            }
            task = new VanillaCreationTask(mc, loader->uid, lv);
            suggested = QString("%1 %2").arg(mcVersion, loader->name);
        } else {
            task = new VanillaCreationTask(mc);
        }
        task->setName(name.isEmpty() ? suggested : name);
        task->setOriginalName(suggested, QString());
        task->setGroup(group);
        task->setIcon("default");

        auto createdId = std::make_shared<QString>();
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this, [createdId](const QString& id) { *createdId = id; });
        auto wrapped = Task::Ptr(APPLICATION->instances()->wrapInstanceTask(task));
        connect(wrapped.get(), &Task::finished, this, [wrapped, createdId, conn, reply] {
            QObject::disconnect(*conn);
            if (!wrapped->wasSuccessful()) {
                return reply({}, wrapped->failReason().isEmpty() ? QString("The instance could not be created") : wrapped->failReason());
            }
            auto* inst = QmlResources::findInstance(*createdId);
            reply(inst ? QJsonValue(instanceSummary(inst)) : QJsonValue(QJsonObject{ { "id", *createdId } }), {});
        });
        TaskCenter::instance()->track(wrapped, tr("Installing %1").arg(name.isEmpty() ? suggested : name), "create");
        wrapped->start();
    };

    // Both lists loaded (one after the other), then create
    if (loaderList) {
        whenLoaded(mcList, this, [this, loaderList, proceed] { whenLoaded(loaderList, this, proceed); });
    } else {
        whenLoaded(mcList, this, proceed);
    }
}

void ControlServer::deleteInstance(MinecraftInstance* inst, const Reply& reply)
{
    if (inst->isRunning()) {
        return reply({}, "Stop the game first");
    }
    auto* ui = APPLICATION->qmlPrompts();
    if (!ui) {
        return reply({}, "The launcher window is not available to confirm the deletion");
    }
    const QString id = inst->id();
    const QString name = inst->name();
    // Deferred so this reply path isn't inside the nested loop of the question
    QTimer::singleShot(0, this, [id, name, reply] {
        auto* prompts = APPLICATION->qmlPrompts();
        const int answer = prompts ? prompts->ask(QObject::tr("Delete “%1”?").arg(name),
                                                  QObject::tr("An AI assistant asked to delete this instance with all its worlds, "
                                                              "mods and settings. It goes to the trash where possible."),
                                                  { QObject::tr("Delete"), QObject::tr("Keep it") }, "danger")
                                   : 1;
        if (answer != 0) {
            return reply(QJsonObject{ { "deleted", false }, { "reason", "the player kept it" } }, {});
        }
        const bool trashed = APPLICATION->instances()->trashInstance(id);
        if (!trashed) {
            APPLICATION->instances()->deleteInstance(id);
        }
        reply(QJsonObject{ { "deleted", true }, { "to_trash", trashed } }, {});
    });
}

void ControlServer::setInstanceSettings(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply)
{
    auto s = inst->settings();
    if (args.contains("max_memory_mb") || args.contains("min_memory_mb")) {
        const int maxMb = args.value("max_memory_mb").toInt(s->get("MaxMemAlloc").toInt());
        const int minMb = args.value("min_memory_mb").toInt(qMin(s->get("MinMemAlloc").toInt(), maxMb));
        if (maxMb < 512 || minMb < 128 || minMb > maxMb) {
            return reply({}, "Memory must be at least 512 MB maximum, 128 MB minimum, and minimum ≤ maximum");
        }
        s->set("OverrideMemory", true);
        s->set("MaxMemAlloc", maxMb);
        s->set("MinMemAlloc", minMb);
    }
    if (args.contains("java_args")) {
        const QString jvm = args.value("java_args").toString();
        s->set("OverrideJavaArgs", !jvm.isEmpty());
        s->set("JvmArgs", jvm);
    }
    getInstance(inst, reply);
}

// ---- mods --------------------------------------------------------------------------------------

void ControlServer::listMods(MinecraftInstance* inst, const QString& kind, const Reply& reply)
{
    auto* folder = folderFor(inst, kind);
    if (!folder) {
        return reply({}, "This instance has no such folder");
    }
    QPointer<ResourceFolderModel> guard(folder);
    afterScan(folder, this, [guard, reply] {
        if (!guard) {
            return reply({}, "The folder went away");
        }
        QJsonArray out;
        for (int i = 0; i < static_cast<int>(guard->size()); ++i) {
            const auto& r = guard->at(i);
            out.append(QJsonObject{
                { "name", r.name() },
                { "version", r.version() },
                { "file", r.fileinfo().fileName() },
                { "enabled", r.enabled() },
                { "source", r.provider() },
            });
        }
        reply(out, {});
    });
}

SearchModel* ControlServer::searchModel(MinecraftInstance* inst, const QString& kind, const QString& platform, const QString& query)
{
    // One model per search, so install_mods can use the results of any recent search
    const QString key = inst->id() + '|' + kind + '|' + platform + '|' + query.trimmed().toLower();
    if (auto m = m_searches.value(key)) {
        return m;
    }
    auto* m = new SearchModel(this);
    m->setInstanceId(inst->id());
    m->setKind(kind);
    m->setPlatform(platform);
    m->setQuery(query);
    m_searches.insert(key, m);
    m_searchOrder.append(key);
    while (m_searchOrder.size() > 30) {  // keep the last 30 searches
        if (auto old = m_searches.take(m_searchOrder.takeFirst())) {
            old->deleteLater();
        }
    }
    return m;
}

void ControlServer::searchMods(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply)
{
    const QString kind = args.value("kind").toString("mods");
    const QString platform = args.value("platform").toString("modrinth");
    const int limit = qBound(1, args.value("limit").toInt(15), 50);
    auto* m = searchModel(inst, kind, platform, args.value("query").toString());
    QPointer<SearchModel> guard(m);
    auto respond = [guard, limit, reply] {
        if (!guard) {
            return;
        }
        if (!guard->error().isEmpty()) {
            return reply({}, "Search failed: " + guard->error());
        }
        QJsonArray out;
        for (int row = 0; row < guard->rowCount() && out.size() < limit; ++row) {
            const auto idx = guard->index(row);
            out.append(QJsonObject{
                { "project_id", guard->data(idx, SearchModel::ProjectIdRole).toString() },
                { "title", guard->data(idx, SearchModel::TitleRole).toString() },
                { "author", guard->data(idx, SearchModel::AuthorRole).toString() },
                { "summary", guard->data(idx, SearchModel::SummaryRole).toString() },
                { "downloads", guard->data(idx, SearchModel::DownloadsRole).toLongLong() },
                { "state", guard->data(idx, SearchModel::StateRole).toString().isEmpty()
                               ? QString("not installed")
                               : guard->data(idx, SearchModel::StateRole).toString() },
                { "url", guard->data(idx, SearchModel::UrlRole).toString() },
            });
        }
        reply(QJsonObject{ { "for", guard->loaderText().isEmpty() ? guard->gameVersion()
                                                                   : guard->gameVersion() + " " + guard->loaderText() },
                           { "results", out } },
              {});
    };
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m, &SearchModel::busyChanged, this, [guard, conn, respond] {
        if (guard && !guard->busy()) {
            QObject::disconnect(*conn);
            respond();
        }
    });
    m->search();
    if (!m->busy()) {
        disconnect(*conn);
        respond();
    }
}

void ControlServer::installMods(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply)
{
    const QString kind = args.value("kind").toString("mods");
    const QString platform = args.value("platform").toString("modrinth");
    QStringList ids;
    for (const auto& v : args.value("project_ids").toArray()) {
        ids << v.toString();
    }
    ids.removeAll(QString());
    if (ids.isEmpty()) {
        return reply({}, "project_ids is empty");
    }
    // Which recent search of this instance/kind/platform holds each project
    const QString prefix = inst->id() + '|' + kind + '|' + platform + '|';
    QStringList unknown;
    QList<QPair<SearchModel*, QString>> picks;
    for (const auto& id : ids) {
        SearchModel* holder = nullptr;
        for (auto it = m_searchOrder.crbegin(); it != m_searchOrder.crend() && !holder; ++it) {
            auto candidate = m_searches.value(*it);
            if (it->startsWith(prefix) && candidate && candidate->rowOfProject(id) >= 0) {
                holder = candidate;
            }
        }
        if (!holder) {
            unknown << id;
        } else {
            picks.append({ holder, id });
        }
    }
    if (!unknown.isEmpty()) {
        return reply({}, QString("Not in the recent search results for this instance: %1. Run search_mods first (same kind and "
                                 "platform).")
                             .arg(unknown.join(", ")));
    }
    // Install through one model so the queue runs one at a time: copy picks into the first holder when they all match,
    // otherwise run each holder's batch (they still install one after another per holder).
    QList<SearchModel*> holders;
    for (const auto& p : picks) {
        if (!holders.contains(p.first)) {
            holders.append(p.first);
            p.first->clearPicks();
        }
        if (p.first->projectState(p.second).isEmpty()) {
            p.first->togglePick(p.first->rowOfProject(p.second));
        }
    }
    auto messages = std::make_shared<QStringList>();
    QList<QMetaObject::Connection> msgConns;
    for (auto* h : holders) {
        msgConns << connect(h, &SearchModel::message, this, [messages](const QString& text, bool) { messages->append(text); });
        h->installPicked();
    }

    // Poll until none of them is queued or installing any more
    QList<QPointer<SearchModel>> guards;
    for (const auto& p : picks) {
        guards << QPointer<SearchModel>(p.first);
    }
    auto* timer = new QTimer(this);
    timer->setInterval(500);
    auto deadline = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [timer, guards, picks, messages, msgConns, deadline, reply] {
        auto stateOf = [&](int i) { return guards.at(i) ? guards.at(i)->projectState(picks.at(i).second) : QString(); };
        bool busy = false;
        for (int i = 0; i < picks.size(); ++i) {
            const QString st = stateOf(i);
            busy = busy || st == "installing" || st == "queued";
        }
        if (busy && ++*deadline < 20 * 60 * 2) {  // 20 minutes at most
            return;
        }
        timer->stop();
        timer->deleteLater();
        for (const auto& c : msgConns) {
            QObject::disconnect(c);
        }
        QJsonArray results;
        for (int i = 0; i < picks.size(); ++i) {
            results.append(QJsonObject{ { "project_id", picks.at(i).second }, { "installed", stateOf(i) == "installed" } });
        }
        reply(QJsonObject{ { "results", results }, { "messages", QJsonArray::fromStringList(*messages) } }, {});
    });
    timer->start();
}

void ControlServer::changeMod(MinecraftInstance* inst, const QJsonObject& args, bool remove, const Reply& reply)
{
    const QString kind = args.value("kind").toString("mods");
    const QString file = args.value("file").toString();
    auto* folder = folderFor(inst, kind);
    if (!folder || file.isEmpty()) {
        return reply({}, "file is required");
    }
    QPointer<ResourceFolderModel> guard(folder);
    const bool enable = args.value("enabled").toBool(true);
    afterScan(folder, this, [this, guard, file, remove, enable, reply] {
        if (!guard) {
            return reply({}, "The folder went away");
        }
        int row = -1;
        for (int i = 0; i < static_cast<int>(guard->size()); ++i) {
            const QString name = guard->at(i).fileinfo().fileName();
            if (name == file || name == file + ".disabled" || name + ".disabled" == file) {
                row = i;
                break;
            }
        }
        if (row < 0) {
            return reply({}, QString("No installed file \"%1\". Use list_mods.").arg(file));
        }
        const QString path = guard->at(row).fileinfo().absoluteFilePath();
        if (remove) {
            if (!FS::trash(path)) {
                return reply({}, "The file could not be moved to the trash");
            }
            guard->update();
            return reply(QJsonObject{ { "removed", QFileInfo(path).fileName() } }, {});
        }
        if (guard->at(row).enabled() == enable) {
            return reply(QJsonObject{ { "file", QFileInfo(path).fileName() }, { "enabled", enable } }, {});
        }
        guard->setResourceEnabled({ guard->index(row, 0) }, enable ? EnableAction::ENABLE : EnableAction::DISABLE);
        afterScan(guard, this, [reply, enable] {
            reply(QJsonObject{ { "enabled", enable } }, {});
        });
    });
}

// ---- worlds, launch, logs ----------------------------------------------------------------------

void ControlServer::listWorlds(MinecraftInstance* inst, const Reply& reply)
{
    auto* worlds = inst->worldList();
    if (!worlds) {
        return reply(QJsonArray{}, {});
    }
    worlds->update();
    QJsonArray out;
    for (const auto& w : worlds->allWorlds()) {
        out.append(QJsonObject{
            { "folder", w.folderName() },
            { "name", w.name() },
            { "last_played", w.lastPlayed().toString(Qt::ISODate) },
        });
    }
    reply(out, {});
}

void ControlServer::launch(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply)
{
    if (inst->isRunning()) {
        return reply({}, "This instance is already running");
    }
    std::shared_ptr<MinecraftTarget> target;
    if (const QString server = args.value("server").toString().trimmed(); !server.isEmpty()) {
        target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(server, false));
    } else if (const QString world = args.value("world").toString().trimmed(); !world.isEmpty()) {
        target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(world, true));
    }
    StoreBridge::beforeLaunch(inst);
    const bool ok = APPLICATION->launch(inst, args.value("offline").toBool(false) ? LaunchMode::Offline : LaunchMode::Normal, target);
    reply(QJsonObject{ { "started", ok } }, ok ? QString() : "The launch did not start (check accounts with list_accounts)");
}

void ControlServer::getLog(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply)
{
    GameLog log;
    log.setInstanceId(inst->id());
    if (log.source() == "none" && !log.loadLastLogFile()) {
        return reply(QJsonObject{ { "source", "" }, { "total_lines", 0 }, { "text", "" },
                                  { "note", "No game log yet: this instance has not been played (or its log was cleared)." } },
                     {});
    }
    const QString level = args.value("level").toString("all");
    if (level != "all") {
        log.setShowLauncher(false);
        log.setShowInfo(false);
        log.setShowWarnings(level == "warning");
    }
    QStringList lines = log.text(level != "all").split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
        lines.removeLast();
    }
    const int want = qBound(1, args.value("lines").toInt(200), 5000);
    const int total = static_cast<int>(lines.size());
    if (total > want) {
        lines = lines.mid(total - want);
    }
    reply(QJsonObject{ { "source", log.source() == "live" ? "live output" : log.fileName() },
                       { "total_lines", log.totalLines() },
                       { "warnings", log.warningCount() },
                       { "errors", log.errorCount() },
                       { "text", lines.join('\n') } },
          {});
}

void ControlServer::diagnose(MinecraftInstance* inst, const Reply& reply)
{
    auto* doctor = CrashDoctor::instance();
    if (!doctor->analyzeInstance(inst->id())) {
        return reply({}, "There is no game log to look at yet");
    }
    reply(QJsonObject::fromVariantMap(doctor->report()), {});
}

// ---- accounts ----------------------------------------------------------------------------------

void ControlServer::listAccounts(const Reply& reply)
{
    auto* accounts = APPLICATION->accounts();
    auto def = accounts->defaultAccount();
    QJsonArray out;
    for (int i = 0; i < accounts->count(); ++i) {
        auto a = accounts->at(i);
        out.append(QJsonObject{
            { "name", a->profileName() },
            { "type", a->accountType() == AccountType::MSA ? "microsoft" : "offline" },
            { "default", a == def },
        });
    }
    reply(out, {});
}

void ControlServer::setDefaultAccount(const QJsonObject& args, const Reply& reply)
{
    const QString name = args.value("name").toString();
    auto* accounts = APPLICATION->accounts();
    for (int i = 0; i < accounts->count(); ++i) {
        auto a = accounts->at(i);
        if (a->profileName().compare(name, Qt::CaseInsensitive) == 0) {
            accounts->setDefaultAccount(a);
            return reply(QJsonObject{ { "default_account", a->profileName() } }, {});
        }
    }
    reply({}, QString("No account \"%1\". Use list_accounts.").arg(name));
}
