// SPDX-License-Identifier: GPL-3.0-only
#include "LauncherImport.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QtConcurrent/QtConcurrentRun>
#include <filesystem>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"

namespace LauncherImport {
namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ToolsBridge", text);
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

/// key=value file without sections (instance.cfg, prismlauncher.cfg)
QHash<QString, QString> readCfg(const QString& path)
{
    QHash<QString, QString> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        const int eq = line.indexOf('=');
        if (eq <= 0 || line.startsWith('[')) {
            continue;
        }
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
            value = value.mid(1, value.size() - 2).replace("\\\"", "\"").replace("\\\\", "\\");
        }
        out.insert(line.left(eq).trimmed(), value);
    }
    return out;
}

QString canonical(const QString& path)
{
    const QString c = QFileInfo(path).canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : c;
}

QStringList dataDirs()
{
    QStringList out;
    const QString home = QDir::homePath();
#if defined(Q_OS_MACOS)
    out << home + "/Library/Application Support";
#elif defined(Q_OS_WIN)
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        out << appData;
    }
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty()) {
        out << local;
    }
#else
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    out << (xdg.isEmpty() ? home + "/.local/share" : xdg);
#endif
    return out;
}

QVariantMap entry(const QString& launcher, const QString& name, const QString& path, const QString& gameDir, const QString& mc,
                  const QString& loader, const QString& loaderVersion, const QString& format)
{
    QVariantMap e{ { "key", launcher + "|" + canonical(path) },
                   { "launcher", launcher },
                   { "name", name },
                   { "path", path },
                   { "gameDir", gameDir },
                   { "mc", mc },
                   { "loader", loader },
                   { "loaderVersion", loaderVersion },
                   { "format", format },
                   { "importable", !mc.isEmpty() && QFileInfo(format == "prism" ? path : gameDir).isDir() },
                   { "note", QString() } };
    if (mc.isEmpty()) {
        e["note"] = tr("Can't tell which Minecraft version it uses.");
    } else if (!loader.isEmpty() && loaderVersion.isEmpty()) {
        e["importable"] = false;
        e["note"] = tr("Can't tell which mod loader version it uses.");
    }
    return e;
}

// ---- MultiMC / PolyMC / Prism --------------------------------------------------------------

QVariantMap prismInstance(const QString& launcher, const QString& dir)
{
    const auto cfg = readCfg(dir + "/instance.cfg");
    QString mc, loader, loaderVersion;
    static const QHash<QString, QString> loaderUids = { { "net.fabricmc.fabric-loader", "fabric" },
                                                        { "org.quiltmc.quilt-loader", "quilt" },
                                                        { "net.minecraftforge", "forge" },
                                                        { "net.neoforged", "neoforge" } };
    for (const auto& c : readJson(dir + "/mmc-pack.json").value("components").toArray()) {
        const auto o = c.toObject();
        const QString uid = o.value("uid").toString();
        const QString v = o.value("version").toString(o.value("cachedVersion").toString());
        if (uid == "net.minecraft") {
            mc = v;
        } else if (loaderUids.contains(uid)) {
            loader = loaderUids.value(uid);
            loaderVersion = v;
        }
    }
    const QString game = QFileInfo::exists(dir + "/.minecraft") ? dir + "/.minecraft" : dir + "/minecraft";
    return entry(launcher, cfg.value("name", QFileInfo(dir).fileName()), dir, game, mc, loader, loaderVersion, "prism");
}

QStringList ownInstanceDirs()
{
    QStringList out;
    if (APPLICATION && APPLICATION->instances()) {
        for (const auto& d : APPLICATION->instances()->instanceDirs()) {
            out << canonical(d);
        }
    }
    return out;
}

void scanPrismInstancesDir(const QString& launcher, const QString& instancesDir, QVariantList& out)
{
    if (ownInstanceDirs().contains(canonical(instancesDir))) {
        return;  // that's us
    }
    for (const auto& info : QDir(instancesDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!info.fileName().startsWith('.') && QFileInfo::exists(info.filePath() + "/instance.cfg")) {
            out << prismInstance(launcher, info.filePath());
        }
    }
}

void scanPrismData(const QString& launcher, const QString& dataDir, const QString& cfgName, QVariantList& out)
{
    if (!QFileInfo(dataDir).isDir()) {
        return;
    }
    QString instDir = readCfg(dataDir + "/" + cfgName).value("InstanceDir", "instances");
    if (QDir::isRelativePath(instDir)) {
        instDir = dataDir + "/" + instDir;
    }
    scanPrismInstancesDir(launcher, instDir, out);
}

void detectPrismFamily(QVariantList& out)
{
    struct Variant {
        QString launcher, folder, cfg;
    };
    const QList<Variant> variants = { { "Prism Launcher", "PrismLauncher", "prismlauncher.cfg" },
                                      { "PolyMC", "PolyMC", "polymc.cfg" },
                                      { "MultiMC", "MultiMC", "multimc.cfg" } };
    for (const auto& base : dataDirs()) {
        for (const auto& v : variants) {
            scanPrismData(v.launcher, base + "/" + v.folder, v.cfg, out);
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
            scanPrismData(v.launcher, base + "/" + v.folder.toLower(), v.cfg, out);
#endif
        }
    }
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
    const QString home = QDir::homePath();
    scanPrismData("Prism Launcher", home + "/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher", "prismlauncher.cfg", out);
    scanPrismData("PolyMC", home + "/.var/app/org.polymc.PolyMC/data/PolyMC", "polymc.cfg", out);
#endif
}

// ---- CurseForge app ------------------------------------------------------------------------

QVariantMap curseInstance(const QString& dir)
{
    const QJsonObject o = readJson(dir + "/minecraftinstance.json");
    const QJsonObject base = o.value("baseModLoader").toObject();
    QString mc = o.value("gameVersion").toString(base.value("minecraftVersion").toString());
    QString loader, loaderVersion;
    const QString loaderName = base.value("name").toString();  // forge-47.2.0, fabric-0.15.7-1.20.1, neoforge-21.1.1
    static const QList<QPair<QString, QString>> prefixes = {
        { "neoforge-", "neoforge" }, { "forge-", "forge" }, { "fabric-", "fabric" }, { "quilt-", "quilt" }
    };
    for (const auto& [prefix, key] : prefixes) {
        if (loaderName.startsWith(prefix)) {
            loader = key;
            loaderVersion = base.value("forgeVersion").toString();
            if (loaderVersion.isEmpty()) {
                loaderVersion = loaderName.mid(prefix.size());
                if (!mc.isEmpty() && loaderVersion.endsWith("-" + mc)) {
                    loaderVersion.chop(mc.size() + 1);
                }
            }
            break;
        }
    }
    return entry("CurseForge", o.value("name").toString(QFileInfo(dir).fileName()), dir, dir, mc, loader, loaderVersion, "folder");
}

void detectCurseForge(QVariantList& out)
{
    const QString home = QDir::homePath();
    QStringList roots;
    roots << home + "/curseforge/minecraft/Instances";
    for (const auto& docs : QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation)) {
        roots << docs + "/curseforge/minecraft/Instances";
    }
    roots.removeDuplicates();
    QSet<QString> seen;
    for (const auto& root : roots) {
        if (seen.contains(canonical(root))) {
            continue;
        }
        seen.insert(canonical(root));
        for (const auto& info : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFileInfo::exists(info.filePath() + "/minecraftinstance.json")) {
                out << curseInstance(info.filePath());
            }
        }
    }
}

// ---- Modrinth App --------------------------------------------------------------------------

QString modrinthLoader(const QString& raw)
{
    const QString l = raw.toLower();
    return l == "vanilla" ? QString() : l;
}

void scanModrinthData(const QString& dataDir, QVariantList& out)
{
    const QString profiles = dataDir + "/profiles";
    if (!QFileInfo(profiles).isDir()) {
        return;
    }
    // Newer app versions keep profile details in app.db (SQLite); read it with the sqlite3 tool when it's there
    QHash<QString, QJsonObject> fromDb;
    const QString db = dataDir + "/app.db";
    const QString sqlite = QStandardPaths::findExecutable("sqlite3");
    if (QFileInfo::exists(db) && !sqlite.isEmpty()) {
        QProcess p;
        p.start(sqlite, { "-readonly", "-json", db, "select path, name, game_version, mod_loader, mod_loader_version from profiles" });
        if (p.waitForFinished(3000) && p.exitCode() == 0) {
            for (const auto& row : QJsonDocument::fromJson(p.readAllStandardOutput()).array()) {
                const auto o = row.toObject();
                fromDb.insert(o.value("path").toString(), o);
            }
        } else {
            p.kill();
        }
    }
    for (const auto& info : QDir(profiles).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = info.filePath();
        QString name = info.fileName(), mc, loader, loaderVersion;
        if (fromDb.contains(info.fileName())) {
            const auto o = fromDb.value(info.fileName());
            name = o.value("name").toString(name);
            mc = o.value("game_version").toString();
            loader = modrinthLoader(o.value("mod_loader").toString());
            loaderVersion = o.value("mod_loader_version").toString();
        } else if (QFileInfo::exists(dir + "/profile.json")) {
            const auto meta = readJson(dir + "/profile.json").value("metadata").toObject();
            name = meta.value("name").toString(name);
            mc = meta.value("game_version").toString();
            loader = modrinthLoader(meta.value("loader").toString());
            const auto lv = meta.value("loader_version");
            loaderVersion = lv.isObject() ? lv.toObject().value("id").toString() : lv.toString();
        }
        out << entry("Modrinth App", name, dir, dir, mc, loader, loaderVersion, "folder");
    }
}

void detectModrinth(QVariantList& out)
{
    for (const auto& base : dataDirs()) {
        scanModrinthData(base + "/ModrinthApp", out);
        scanModrinthData(base + "/com.modrinth.theseus", out);
    }
}

// ---- ATLauncher ----------------------------------------------------------------------------

QVariantMap atlInstance(const QString& dir)
{
    const QJsonObject o = readJson(dir + "/instance.json");
    const QJsonObject launcher = o.value("launcher").toObject();
    const QJsonObject lv = launcher.value("loaderVersion").toObject();
    QString loader = lv.value("type").toString().toLower();
    if (loader == "legacyfabric") {
        loader.clear();
    }
    return entry("ATLauncher", launcher.value("name").toString(QFileInfo(dir).fileName()), dir, dir, o.value("id").toString(), loader,
                 loader.isEmpty() ? QString() : lv.value("version").toString(), "folder");
}

void detectATLauncher(QVariantList& out)
{
    QStringList roots;
    for (const auto& base : dataDirs()) {
        roots << base + "/ATLauncher/instances";
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
        roots << base + "/atlauncher/instances";
#endif
    }
    roots << QDir::homePath() + "/.atlauncher/instances";
    for (const auto& root : roots) {
        for (const auto& info : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFileInfo::exists(info.filePath() + "/instance.json")) {
                out << atlInstance(info.filePath());
            }
        }
    }
}

// ---- Official launcher ---------------------------------------------------------------------

void resolveVanillaVersion(const QString& root, const QString& versionId, QString& mc, QString& loader, QString& loaderVersion)
{
    const QJsonObject v = readJson(root + "/versions/" + versionId + "/" + versionId + ".json");
    mc = v.value("inheritsFrom").toString();
    if (mc.isEmpty()) {
        mc = v.isEmpty() ? QString() : versionId;
        return;
    }
    QRegularExpressionMatch m;
    if ((m = QRegularExpression(R"(^fabric-loader-([^-]+)-)").match(versionId)).hasMatch()) {
        loader = "fabric";
        loaderVersion = m.captured(1);
    } else if ((m = QRegularExpression(R"(^quilt-loader-([^-]+)-)").match(versionId)).hasMatch()) {
        loader = "quilt";
        loaderVersion = m.captured(1);
    } else if ((m = QRegularExpression(R"(^neoforge-(.+)$)").match(versionId)).hasMatch()) {
        loader = "neoforge";
        loaderVersion = m.captured(1);
    } else if ((m = QRegularExpression(R"(-forge-?(?:[\d.]+-)?([\d.]+)$)").match(versionId)).hasMatch()) {
        loader = "forge";
        loaderVersion = m.captured(1);
    } else {
        mc.clear();  // some other modified version we can't rebuild
    }
}

void scanOfficial(const QString& root, QVariantList& out)
{
    const QJsonObject profiles = readJson(root + "/launcher_profiles.json").value("profiles").toObject();
    QString latestRelease;  // newest release that's actually installed
    for (const auto& info : QDir(root + "/versions").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        static const QRegularExpression release(R"(^\d+\.\d+(\.\d+)?$)");
        if (release.match(info.fileName()).hasMatch() && QFileInfo::exists(info.filePath() + "/" + info.fileName() + ".json") &&
            (latestRelease.isEmpty() || QVersionNumber::fromString(info.fileName()) > QVersionNumber::fromString(latestRelease))) {
            latestRelease = info.fileName();
        }
    }
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        const QJsonObject p = it.value().toObject();
        const QString type = p.value("type").toString();
        const QString versionId = p.value("lastVersionId").toString();
        QString name = p.value("name").toString();
        QString mc, loader, loaderVersion;
        if (type == "latest-snapshot" || versionId == "latest-snapshot") {
            continue;
        }
        if (type == "latest-release" || versionId == "latest-release") {
            mc = latestRelease;
            if (name.isEmpty()) {
                name = tr("Latest release");
            }
        } else {
            resolveVanillaVersion(root, versionId, mc, loader, loaderVersion);
        }
        if (name.isEmpty()) {
            name = versionId;
        }
        QString gameDir = p.value("gameDir").toString();
        if (gameDir.isEmpty()) {
            gameDir = root;
        }
        auto e = entry(tr("Minecraft Launcher"), name, root + "#" + it.key(), gameDir, mc, loader, loaderVersion, "folder");
        e["key"] = "official|" + canonical(root) + "#" + it.key();
        out << e;
    }
}

void detectOfficial(QVariantList& out)
{
#if defined(Q_OS_MACOS)
    scanOfficial(QDir::homePath() + "/Library/Application Support/minecraft", out);
#elif defined(Q_OS_WIN)
    scanOfficial(qEnvironmentVariable("APPDATA") + "/.minecraft", out);
#else
    scanOfficial(QDir::homePath() + "/.minecraft", out);
#endif
}

// ---- copying -------------------------------------------------------------------------------

const QSet<QString>& skippedTopLevel()
{
    // launcher files and caches that don't belong in an instance
    static const QSet<QString> s = { "versions",
                                     "libraries",
                                     "assets",
                                     "runtime",
                                     "logs",
                                     "crash-reports",
                                     "webcache",
                                     "webcache2",
                                     "bin",
                                     "natives",
                                     ".fabric",
                                     ".mixin.out",
                                     ".cache",
                                     ".DS_Store",
                                     "launcher_accounts.json",
                                     "launcher_accounts_microsoft_store.json",
                                     "launcher_profiles.json",
                                     "launcher_settings.json",
                                     "launcher_log.txt",
                                     "launcher_cef_log.txt",
                                     "launcher_msa_credentials.bin",
                                     "launcher_ui_state.json",
                                     "launcher_product_state.json",
                                     "launcher_entitlements.json",
                                     "launcher_gamer_pics.json",
                                     "launcher_skins.json",
                                     "treatment_tags.json",
                                     "minecraftinstance.json",
                                     "instance.json",
                                     "profile.json" };
    return s;
}

/// Copies src into dst. Returns an error, or "" when done (or cancelled).
QString copyTree(const QString& src, const QString& dst, const QSet<QString>& skipTop, const std::atomic<bool>& cancel)
{
    QDir srcDir(src);
    if (!QDir().mkpath(dst)) {
        return tr("Couldn't create %1").arg(dst);
    }
    for (const auto& top : srcDir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (cancel) {
            return {};
        }
        if (skipTop.contains(top.fileName())) {
            continue;
        }
        const QString target = dst + "/" + top.fileName();
        if (top.isDir() && !top.isSymLink()) {
            QDir().mkpath(target);
            QDirIterator it(top.filePath(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (cancel) {
                    return {};
                }
                it.next();
                const QFileInfo info = it.fileInfo();
                const QString rel = QDir(top.filePath()).relativeFilePath(info.filePath());
                const QString out = target + "/" + rel;
                if (info.isDir() && !info.isSymLink()) {
                    QDir().mkpath(out);
                } else if (info.exists() && !QFile::copy(info.filePath(), out)) {
                    return tr("Couldn't copy %1").arg(info.filePath());
                }
            }
        } else if (top.exists() && !QFile::copy(top.filePath(), target)) {
            return tr("Couldn't copy %1").arg(top.filePath());
        }
    }
    return {};
}

bool linkDir(const QString& target, const QString& link)
{
    std::error_code ec;
    std::filesystem::create_directory_symlink(std::filesystem::path(target.toStdU16String()), std::filesystem::path(link.toStdU16String()),
                                              ec);
    if (ec) {
        qWarning() << "[tools] couldn't link" << link << "to" << target << ":" << QString::fromStdString(ec.message());
        return false;
    }
    return true;
}

QString loaderUid(const QString& loader)
{
    if (loader == "fabric") {
        return "net.fabricmc.fabric-loader";
    }
    if (loader == "quilt") {
        return "org.quiltmc.quilt-loader";
    }
    if (loader == "forge") {
        return "net.minecraftforge";
    }
    if (loader == "neoforge") {
        return "net.neoforged";
    }
    return {};
}

void dedupe(QVariantList& list)
{
    QSet<QString> keys;
    QVariantList out;
    for (const auto& v : list) {
        const QString k = v.toMap().value("key").toString();
        if (!keys.contains(k)) {
            keys.insert(k);
            out << v;
        }
    }
    list = out;
}

}  // namespace

QVariantList detect()
{
    QVariantList out;
    detectCurseForge(out);
    detectModrinth(out);
    detectATLauncher(out);
    detectPrismFamily(out);
    detectOfficial(out);
    dedupe(out);
    return out;
}

QVariantList scanFolder(const QString& rawPath)
{
    QVariantList out;
    const QString path = QDir::cleanPath(rawPath);
    const QDir dir(path);
    if (!dir.exists()) {
        return out;
    }
    // one instance
    if (QFileInfo::exists(path + "/instance.cfg")) {
        out << prismInstance(tr("Prism / MultiMC"), path);
    } else if (QFileInfo::exists(path + "/minecraftinstance.json")) {
        out << curseInstance(path);
    } else if (QFileInfo::exists(path + "/instance.json")) {
        out << atlInstance(path);
    } else if (QFileInfo::exists(path + "/launcher_profiles.json")) {
        scanOfficial(path, out);
    } else if (QFileInfo(path + "/profiles").isDir()) {
        scanModrinthData(path, out);
    } else {
        // a launcher's data folder or an instances folder
        for (const char* cfg : { "prismlauncher.cfg", "polymc.cfg", "multimc.cfg" }) {
            if (QFileInfo::exists(path + "/" + cfg)) {
                scanPrismData(tr("Prism / MultiMC"), path, cfg, out);
            }
        }
        if (out.isEmpty()) {
            for (const auto& info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString sub = info.filePath();
                if (QFileInfo::exists(sub + "/instance.cfg")) {
                    if (!ownInstanceDirs().contains(canonical(path))) {
                        out << prismInstance(tr("Prism / MultiMC"), sub);
                    }
                } else if (QFileInfo::exists(sub + "/minecraftinstance.json")) {
                    out << curseInstance(sub);
                } else if (QFileInfo::exists(sub + "/instance.json")) {
                    out << atlInstance(sub);
                } else if (info.fileName() == "instances" || info.fileName() == "Instances") {
                    for (const auto& v : scanFolder(sub)) {
                        out << v;
                    }
                }
            }
        }
    }
    dedupe(out);
    return out;
}

InstanceTask* makeTask(const QVariantMap& entry, bool link)
{
    return new ForeignImportTask(entry, link);
}

bool ForeignImportTask::abort()
{
    *m_cancel = true;
    return InstanceTask::abort();
}

void ForeignImportTask::executeTask()
{
    const QString format = m_entry.value("format").toString();
    const QString source = m_entry.value("path").toString();
    const QString gameDir = m_entry.value("gameDir").toString();
    setStatus(m_link ? tr("Linking %1…").arg(m_entry.value("name").toString()) : tr("Copying %1…").arg(m_entry.value("name").toString()));
    setProgress(0, 0);

    if (format == "folder") {
        // Build a fresh instance around the other launcher's game folder
        auto inst = std::make_unique<MinecraftInstance>(
            m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);
        {
            const SettingsObject::Lock lock(inst->settings());
            auto* components = inst->getPackProfile();
            components->buildingFromScratch();
            components->setComponentVersion("net.minecraft", m_entry.value("mc").toString(), true);
            const QString uid = loaderUid(m_entry.value("loader").toString());
            if (!uid.isEmpty()) {
                components->setComponentVersion(uid, m_entry.value("loaderVersion").toString());
            }
            inst->setName(name());
            inst->setIconKey(m_instIcon);
            components->saveNow();
        }
    }

    const bool prism = format == "prism";
    const QString stagingGame = FS::PathCombine(m_stagingPath, prism ? QFileInfo(gameDir).fileName() : QString(".minecraft"));
    auto cancel = m_cancel;
    const bool wantLink = m_link;
    const QString staging = m_stagingPath;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString error = watcher->result();
        watcher->deleteLater();
        if (*m_cancel) {
            emitAborted();
        } else if (!error.isEmpty()) {
            emitFailed(error);
        } else {
            finishInstance();
        }
    });
    watcher->setFuture(QtConcurrent::run([=]() -> QString {
        if (prism) {
            // everything but the game folder, then the game folder itself
            QSet<QString> skip = { QFileInfo(gameDir).fileName() };
            if (const QString e = copyTree(source, staging, skip, *cancel); !e.isEmpty()) {
                return e;
            }
        }
        if (!QFileInfo(gameDir).isDir()) {
            QDir().mkpath(stagingGame);
            return {};
        }
        if (wantLink && linkDir(canonical(gameDir), stagingGame)) {
            return {};
        }
        return copyTree(gameDir, stagingGame, prism ? QSet<QString>{} : skippedTopLevel(), *cancel);
    }));
}

void ForeignImportTask::finishInstance()
{
    if (m_entry.value("format").toString() == "prism") {
        // The copied instance keeps its own settings; give it the chosen name and an icon we have
        MinecraftInstance inst(m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")),
                               m_stagingPath);
        inst.setName(name());
        inst.setIconKey(m_instIcon);
        inst.settings()->set("linkedInstances", "[]");
    }
    emitSucceeded();
}

}  // namespace LauncherImport
