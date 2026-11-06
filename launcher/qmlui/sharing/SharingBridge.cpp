// SPDX-License-Identifier: GPL-3.0-only
#include "SharingBridge.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <zlib.h>
#include <algorithm>

#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "ResourceDownloadTask.h"
#include "meta/Index.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/helpers/HashUtils.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/packwiz/Packwiz.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"
#include "settings/SettingsObject.h"
#include "tasks/ConcurrentTask.h"

namespace {
struct LoaderUid {
    const char* key;
    const char* uid;
};
constexpr LoaderUid kLoaders[] = {
    { "fabric", "net.fabricmc.fabric-loader" },
    { "quilt", "org.quiltmc.quilt-loader" },
    { "neoforge", "net.neoforged" },
    { "forge", "net.minecraftforge" },
};
const QStringList kKinds{ "mods", "resourcepacks", "shaderpacks" };
const QString kLinkPrefix = QStringLiteral("fashion://pack/");

QString loaderUid(const QString& key)
{
    for (const auto& l : kLoaders) {
        if (key == QLatin1String(l.key)) {
            return l.uid;
        }
    }
    return {};
}

QString sideName(ModPlatform::SideType s)
{
    if (s == ModPlatform::SideType::ClientSide) {
        return "client";
    }
    if (s == ModPlatform::SideType::ServerSide) {
        return "server";
    }
    if (s == ModPlatform::SideType::UniversalSide) {
        return "both";
    }
    return "unknown";
}

ModPlatform::SideType sideType(const QString& s)
{
    if (s == "client") {
        return ModPlatform::SideType::ClientSide;
    }
    if (s == "server") {
        return ModPlatform::SideType::ServerSide;
    }
    return ModPlatform::SideType::UniversalSide;
}

// Who needs the mod, from Modrinth's client_side/server_side ("required" | "optional" | "unsupported" | "unknown").
// "Optional" on one side while required on the other means that side can do without it.
QString modrinthSide(const QString& c, const QString& s)
{
    if (s == "unsupported" && c != "unsupported") {
        return "client";
    }
    if (c == "unsupported" && s != "unsupported") {
        return "server";
    }
    if (c == "required" && s == "optional") {
        return "client";
    }
    if (s == "required" && c == "optional") {
        return "server";
    }
    if (c == "unknown" && s == "unknown") {
        return "unknown";
    }
    return "both";
}

QString keyFor(const QString& platform, const QString& projectId, const QString& file)
{
    return platform == "local" || projectId.isEmpty() ? "file:" + file : platform + ":" + projectId;
}

QString slugify(const QString& name)
{
    QString s = name.toLower();
    s.replace(QRegularExpression("[^a-z0-9]+"), "-");
    s.remove(QRegularExpression("^-+|-+$"));
    return s.isEmpty() ? QStringLiteral("mod") : s;
}

QByteArray zlibCompress(const QByteArray& in)
{
    uLongf size = compressBound(static_cast<uLong>(in.size()));
    QByteArray out(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(in.constData()),
                  static_cast<uLong>(in.size()), 9) != Z_OK) {
        return {};
    }
    out.resize(static_cast<qsizetype>(size));
    return out;
}

QByteArray zlibUncompress(const QByteArray& in)
{
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        return {};
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    zs.avail_in = static_cast<uInt>(in.size());
    QByteArray out;
    char buf[16384];
    int ret = Z_OK;
    while (ret == Z_OK) {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        out.append(buf, static_cast<qsizetype>(sizeof(buf) - zs.avail_out));
        if (out.size() > 8 * 1024 * 1024) {  // no sane pack is this big
            ret = Z_DATA_ERROR;
        }
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END ? out : QByteArray();
}

QVariantMap modItemFromCompact(const QJsonArray& a, const QString& defaultSide)
{
    // [platform char, projectId, versionId, side char, name, file]
    static const QHash<QString, QString> platforms{ { "m", "modrinth" }, { "c", "curseforge" }, { "l", "local" } };
    static const QHash<QString, QString> sides{ { "c", "client" }, { "s", "server" }, { "b", "both" }, { "u", "unknown" } };
    QVariantMap m;
    m["platform"] = platforms.value(a.at(0).toString(), "local");
    m["projectId"] = a.at(1).toString();
    m["versionId"] = a.at(2).toString();
    m["side"] = sides.value(a.at(3).toString(), defaultSide);
    m["name"] = a.at(4).toString();
    m["file"] = a.at(5).toString();
    m["key"] = keyFor(m["platform"].toString(), m["projectId"].toString(), m["file"].toString());
    return m;
}

QJsonArray modItemToCompact(const QVariantMap& m)
{
    const QString p = m.value("platform").toString();
    const QString s = m.value("side").toString();
    QJsonArray a{ p == "modrinth" ? "m" : p == "curseforge" ? "c" : "l", m.value("projectId").toString(), m.value("versionId").toString(),
                  s.isEmpty() ? QString("u") : s.left(1), m.value("name").toString() };
    if (p == "local") {
        a.append(m.value("file").toString());
    }
    return a;
}

QString cachePath()
{
    return FS::PathCombine(APPLICATION->dataRoot(), "cache", "fashion-sharing.json");
}

int concurrentDownloads()
{
    const int n = APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt();
    return n > 0 ? n : 6;
}
}  // namespace

SharingBridge::SharingBridge(QObject* parent) : QObject(parent)
{
    APPLICATION->settings()->registerSetting("QmlShareIncludeClient", false);
    loadCache();
    if (!qEnvironmentVariableIsEmpty("FFPRISM_SHARE")) {
        QTimer::singleShot(2500, this, &SharingBridge::runAutoTest);
    }
}

SharingBridge::~SharingBridge() = default;

bool SharingBridge::includeClientDefault() const
{
    return APPLICATION->settings()->get("QmlShareIncludeClient").toBool();
}

void SharingBridge::setIncludeClientDefault(bool on)
{
    if (on != includeClientDefault()) {
        APPLICATION->settings()->set("QmlShareIncludeClient", on);
        emit includeClientDefaultChanged();
    }
}

// ---- cache ------------------------------------------------------------------------------------------------------

void SharingBridge::loadCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto obj = QJsonDocument::fromJson(f.readAll()).object();
    m_hashCache = obj.value("hashes").toObject().toVariantMap();
    m_projectCache = obj.value("projects").toObject().toVariantMap();
}

void SharingBridge::saveCache() const
{
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    QJsonObject obj{ { "hashes", QJsonObject::fromVariantMap(m_hashCache) }, { "projects", QJsonObject::fromVariantMap(m_projectCache) } };
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    f.commit();
}

QString SharingBridge::fileSha1(const QString& path)
{
    const QFileInfo fi(path);
    const QString stamp = QString("%1:%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
    const auto it = m_sha1Memo.constFind(path);
    if (it != m_sha1Memo.constEnd() && it->first == stamp) {
        return it->second;
    }
    const QString sha1 = Hashing::hash(path, Hashing::Algorithm::Sha1);
    m_sha1Memo.insert(path, { stamp, sha1 });
    return sha1;
}

// ---- scanning ---------------------------------------------------------------------------------------------------

QVariantList SharingBridge::scanFolder(const QString& dirPath, const QString& kind, bool hashAll)
{
    QVariantList out;
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return out;
    }
    // Prism's per-file metadata (.index/<slug>.pw.toml), keyed by file name
    QHash<QString, Packwiz::V1::Mod> meta;
    const QDir indexDir(dir.absoluteFilePath(".index"));
    for (const auto& toml : indexDir.entryList({ "*.pw.toml" }, QDir::Files)) {
        auto mod = Packwiz::V1::getIndexForMod(indexDir, toml.chopped(8));
        if (mod.isValid() && !mod.filename.isEmpty()) {
            meta.insert(mod.filename, mod);
        }
    }
    const QStringList accepted = kind == "mods" ? QStringList{ "jar", "zip" } : QStringList{ "zip" };
    for (const auto& fi : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (!accepted.contains(fi.suffix().toLower())) {
            continue;  // folders, disabled mods (.disabled), shader configs (.txt)
        }
        QVariantMap m;
        m["file"] = fi.fileName();
        m["size"] = fi.size();
        m["kind"] = kind;
        m["name"] = fi.completeBaseName();
        m["platform"] = "local";
        m["projectId"] = QString();
        m["versionId"] = QString();
        m["side"] = kind == "mods" ? "unknown" : "client";
        QString sha1;
        if (auto it = meta.constFind(fi.fileName()); it != meta.constEnd()) {
            const auto& mod = *it;
            const bool flame = mod.provider == ModPlatform::ResourceProvider::FLAME;
            m["platform"] = flame ? "curseforge" : "modrinth";
            m["projectId"] = mod.projectId.toString();
            m["versionId"] = mod.fileId.toString();
            m["name"] = mod.name.isEmpty() ? fi.completeBaseName() : mod.name;
            m["slug"] = mod.slug;
            m["url"] = mod.url.toString();
            if (kind == "mods") {
                m["side"] = sideName(mod.side);
            }
            if (mod.hashFormat == "sha1") {
                sha1 = mod.hash;
            }
        }
        if (sha1.isEmpty() && (hashAll || m["platform"] == "local")) {
            sha1 = fileSha1(fi.absoluteFilePath());
        }
        m["sha1"] = sha1;
        if (m["platform"] == "local" && !sha1.isEmpty() && m_hashCache.contains(sha1)) {
            const auto hit = m_hashCache.value(sha1).toMap();  // found on Modrinth by its hash
            m["platform"] = "modrinth";
            m["projectId"] = hit.value("projectId");
            m["versionId"] = hit.value("versionId");
            m["url"] = hit.value("url");
        }
        if (m["platform"] == "modrinth") {
            const auto project = m_projectCache.value(m["projectId"].toString()).toMap();
            if (!project.isEmpty()) {
                if (kind == "mods") {
                    m["side"] = project.value("side");
                }
                if (!m.contains("slug") || m["slug"].toString().isEmpty()) {
                    m["slug"] = project.value("slug");
                }
                if (!meta.contains(fi.fileName()) && !project.value("name").toString().isEmpty()) {
                    m["name"] = project.value("name");
                }
            }
        }
        m["key"] = keyFor(m["platform"].toString(), m["projectId"].toString(), m["file"].toString());
        out.append(m);
    }
    return out;
}

QVariantMap SharingBridge::scanInstance(const QString& instanceId, bool hashAll, QVariantMap* versionInfo)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst) {
        return {};
    }
    QVariantMap out;
    out["mods"] = scanFolder(inst->modsRoot(), "mods", hashAll);
    out["resourcepacks"] = scanFolder(inst->resourcePacksDir(), "resourcepacks", hashAll);
    out["shaderpacks"] = scanFolder(inst->shaderPacksDir(), "shaderpacks", hashAll);
    if (versionInfo) {
        auto* profile = inst->getPackProfile();
        if (!profile->getComponent("net.minecraft")) {
            profile->reload(Net::Mode::Offline);  // components load lazily; versions read "" until then
        }
        (*versionInfo)["mc"] = profile->getComponentVersion("net.minecraft");
        (*versionInfo)["loader"] = QString();
        (*versionInfo)["loaderVersion"] = QString();
        for (const auto& l : kLoaders) {
            if (const auto v = profile->getComponentVersion(l.uid); !v.isEmpty()) {
                (*versionInfo)["loader"] = QString(l.key);
                (*versionInfo)["loaderVersion"] = v;
                break;
            }
        }
        (*versionInfo)["name"] = inst->name();
    }
    return out;
}

QString SharingBridge::instanceName(const QString& instanceId) const
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    return inst ? inst->name() : QString();
}

QVariantList SharingBridge::instanceOptions() const
{
    QVariantList out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        out.append(QVariantMap{ { "value", list->at(i)->id() }, { "label", list->at(i)->name() } });
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("label").toString().localeAwareCompare(b.toMap().value("label").toString()) < 0;
    });
    return out;
}

// ---- manifests --------------------------------------------------------------------------------------------------

QVariantMap SharingBridge::createManifest(const QString& instanceId, const QVariantMap& options)
{
    QVariantMap info;
    const auto scanned = scanInstance(instanceId, true, &info);
    if (info.isEmpty()) {
        return {};
    }
    QVariantMap manifest{ { "format", 1 },
                          { "name", info.value("name") },
                          { "mc", info.value("mc") },
                          { "loader", info.value("loader") },
                          { "loaderVersion", info.value("loaderVersion") } };
    for (const auto& k : kKinds) {
        QVariantList items;
        for (const auto& v : scanned.value(k).toList()) {
            auto m = v.toMap();
            m.remove("kind");
            items.append(m);
        }
        manifest[k] = items;
    }
    QVariantMap opts = options;
    if (!opts.contains("includeClient")) {
        opts["includeClient"] = true;
    }
    return filterManifest(manifest, opts);
}

QVariantMap SharingBridge::filterManifest(const QVariantMap& manifest, const QVariantMap& options) const
{
    const bool includeClient = options.value("includeClient", true).toBool();
    const QStringList exclude = options.value("exclude").toStringList();
    QVariantMap out = manifest;
    for (const auto& k : kKinds) {
        QVariantList items;
        if (includeClient || k == "mods") {
            for (const auto& v : manifest.value(k).toList()) {
                const auto m = v.toMap();
                if (exclude.contains(m.value("key").toString())) {
                    continue;
                }
                if (!includeClient && m.value("side").toString() == "client") {
                    continue;
                }
                auto item = m;
                item["id"] = m.value("key");  // alias used by the multiplayer screen
                items.append(item);
            }
        }
        out[k] = items;
    }
    out["includeClient"] = includeClient && manifest.value("includeClient", true).toBool();
    out["minecraft"] = manifest.value("mc");  // alias
    return out;
}

void SharingBridge::analyze(const QString& instanceId)
{
    if (m_analyzeTask) {
        m_analyzeTask->abort();
        m_analyzeTask.reset();
    }
    m_analyzeId = instanceId;
    const auto scanned = scanInstance(instanceId, false);

    QStringList unknownHashes;
    for (const auto& k : kKinds) {
        for (const auto& v : scanned.value(k).toList()) {
            const auto m = v.toMap();
            if (m.value("platform") == "local" && !m.value("sha1").toString().isEmpty()) {
                unknownHashes << m.value("sha1").toString();
            }
        }
    }

    // Step 2: which side each Modrinth project runs on (cached for good; it rarely changes).
    auto fetchProjects = [this, instanceId] {
        const auto scannedNow = scanInstance(instanceId, false);
        QStringList ids;
        for (const auto& k : kKinds) {
            for (const auto& v : scannedNow.value(k).toList()) {
                const auto m = v.toMap();
                const QString id = m.value("projectId").toString();
                if (m.value("platform") == "modrinth" && !id.isEmpty() && !m_projectCache.contains(id) && !ids.contains(id)) {
                    ids << id;
                }
            }
        }
        if (ids.isEmpty()) {
            m_analyzeTask.reset();
            emit analyzingChanged();
            emit analyzed(instanceId);
            return;
        }
        auto [task, response] = ModrinthAPI().getProjects(ids);
        m_analyzeTask = task;
        connect(task.get(), &Task::finished, this, [this, t = task.get(), response, instanceId] {
            if (m_analyzeTask.get() != t) {
                return;
            }
            for (const auto& v : QJsonDocument::fromJson(*response).array()) {
                const auto p = v.toObject();
                m_projectCache[p.value("id").toString()] =
                    QVariantMap{ { "side", modrinthSide(p.value("client_side").toString(), p.value("server_side").toString()) },
                                 { "name", p.value("title").toString() },
                                 { "slug", p.value("slug").toString() } };
            }
            saveCache();
            m_analyzeTask.reset();
            emit analyzingChanged();
            emit analyzed(instanceId);
        });
        task->start();
    };

    if (unknownHashes.isEmpty()) {
        m_analyzeTask = makeShared<ConcurrentTask>();  // placeholder so `analyzing` reads true until step 2 finishes
        emit analyzingChanged();
        fetchProjects();
        return;
    }
    // Step 1: files without metadata — maybe they came from Modrinth anyway.
    auto [task, response] = ModrinthAPI::currentVersions(unknownHashes, "sha1");
    m_analyzeTask = task;
    emit analyzingChanged();
    connect(task.get(), &Task::finished, this, [this, t = task.get(), response, fetchProjects] {
        if (m_analyzeTask.get() != t) {
            return;
        }
        const auto obj = QJsonDocument::fromJson(*response).object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const auto ver = it.value().toObject();
            QVariantMap hit{ { "projectId", ver.value("project_id").toString() }, { "versionId", ver.value("id").toString() } };
            for (const auto& f : ver.value("files").toArray()) {
                const auto file = f.toObject();
                if (file.value("hashes").toObject().value("sha1").toString() == it.key()) {
                    hit["url"] = file.value("url").toString();
                }
            }
            m_hashCache[it.key()] = hit;
        }
        fetchProjects();
    });
    task->start();
}

QVariantList SharingBridge::matchInstances(const QVariantMap& manifest)
{
    struct Scored {
        QVariantMap row;
        int overlap;
    };
    QList<Scored> rows;
    const auto wanted = manifest.value("mods").toList();
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        auto* inst = list->at(i);
        QVariantMap info;
        const auto scanned = scanInstance(inst->id(), false, &info);
        const bool sameVersion = info.value("mc") == manifest.value("mc") && info.value("loader") == manifest.value("loader");

        QHash<QString, QVariantMap> have;
        QSet<QString> haveHashes;
        for (const auto& v : scanned.value("mods").toList()) {
            const auto m = v.toMap();
            have.insert(m.value("key").toString(), m);
            if (!m.value("sha1").toString().isEmpty()) {
                haveHashes.insert(m.value("sha1").toString());
            }
        }
        QVariantList missing;
        QSet<QString> wantedKeys;
        int overlap = 0;
        for (const auto& v : wanted) {
            const auto m = v.toMap();
            const QString key = m.value("key").toString();
            wantedKeys.insert(key);
            const auto it = have.constFind(key);
            const bool sameFile = (it != have.constEnd() && (it->value("versionId") == m.value("versionId") || it->value("file") == m.value("file"))) ||
                                  (!m.value("sha1").toString().isEmpty() && haveHashes.contains(m.value("sha1").toString()));
            if (it != have.constEnd()) {
                ++overlap;
            }
            if (!sameFile) {
                missing.append(m);
            }
        }
        QVariantList extra;
        for (const auto& v : scanned.value("mods").toList()) {
            if (!wantedKeys.contains(v.toMap().value("key").toString())) {
                extra.append(v);
            }
        }
        if (!sameVersion && overlap == 0) {
            continue;
        }
        rows.append({ QVariantMap{ { "instanceId", inst->id() },
                                   { "name", inst->name() },
                                   { "mc", info.value("mc") },
                                   { "loader", info.value("loader") },
                                   { "sameVersion", sameVersion },
                                   { "missing", missing },
                                   { "missingCount", missing.size() },
                                   { "extra", extra } },
                      overlap });
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Scored& a, const Scored& b) {
        const bool sa = a.row.value("sameVersion").toBool(), sb = b.row.value("sameVersion").toBool();
        if (sa != sb) {
            return sa;
        }
        if (a.overlap != b.overlap) {
            return a.overlap > b.overlap;
        }
        return a.row.value("missing").toList().size() < b.row.value("missing").toList().size();
    });
    QVariantList out;
    for (const auto& r : rows) {
        out.append(r.row);
    }
    return out;
}

// ---- links and files --------------------------------------------------------------------------------------------

QString SharingBridge::packLink(const QVariantMap& manifest) const
{
    QJsonObject obj{ { "v", 1 },
                     { "n", manifest.value("name").toString() },
                     { "mc", manifest.value("mc").toString() },
                     { "l", manifest.value("loader").toString() },
                     { "lv", manifest.value("loaderVersion").toString() },
                     { "c", manifest.value("includeClient", true).toBool() } };
    const QHash<QString, QString> shortKeys{ { "mods", "m" }, { "resourcepacks", "r" }, { "shaderpacks", "s" } };
    for (const auto& k : kKinds) {
        QJsonArray arr;
        for (const auto& v : manifest.value(k).toList()) {
            arr.append(modItemToCompact(v.toMap()));
        }
        if (!arr.isEmpty()) {
            obj[shortKeys.value(k)] = arr;
        }
    }
    const auto packed = zlibCompress(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return kLinkPrefix + QString::fromLatin1(packed.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QVariantMap SharingBridge::parseLink(const QString& text) const
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return { { "error", tr("Paste a share link first.") } };
    }
    if (t.startsWith('{')) {
        const auto obj = QJsonDocument::fromJson(t.toUtf8()).object();
        if (obj.value("format").toInt() == 1 && obj.contains("mods")) {
            return obj.toVariantMap();
        }
        return { { "error", tr("This doesn't look like a shared instance.") } };
    }
    static const QRegularExpression re(QStringLiteral("fashion://pack/([A-Za-z0-9_\\-]+)"));
    const auto match = re.match(t);
    if (!match.hasMatch()) {
        return { { "error", tr("This isn't a share link. Links start with fashion://pack/") } };
    }
    const auto raw = QByteArray::fromBase64(match.captured(1).toLatin1(), QByteArray::Base64UrlEncoding);
    const auto obj = QJsonDocument::fromJson(zlibUncompress(raw)).object();
    if (obj.value("v").toInt() != 1) {
        return { { "error", tr("This link is broken or cut off. Ask for it again, or for the file instead.") } };
    }
    QVariantMap manifest{ { "format", 1 },
                          { "name", obj.value("n").toString() },
                          { "mc", obj.value("mc").toString() },
                          { "loader", obj.value("l").toString() },
                          { "loaderVersion", obj.value("lv").toString() },
                          { "includeClient", obj.value("c").toBool(true) } };
    const QHash<QString, QString> shortKeys{ { "mods", "m" }, { "resourcepacks", "r" }, { "shaderpacks", "s" } };
    for (const auto& k : kKinds) {
        QVariantList items;
        for (const auto& v : obj.value(shortKeys.value(k)).toArray()) {
            items.append(modItemFromCompact(v.toArray(), k == "mods" ? "unknown" : "client"));
        }
        manifest[k] = items;
    }
    return manifest;
}

QString SharingBridge::saveFile(const QVariantMap& manifest, const QString& path)
{
    QString target = path;
    if (target.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        QString base = manifest.value("name").toString();
        base.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        target = QFileDialog::getSaveFileName(nullptr, tr("Save shared instance"), FS::PathCombine(dir, base + ".fashpack"),
                                              tr("Fashion pack (*.fashpack)"));
        if (target.isEmpty()) {
            return {};
        }
    }
    if (!target.endsWith(".fashpack", Qt::CaseInsensitive)) {
        target += ".fashpack";
    }
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly)) {
        setError(tr("Couldn't save the file: %1").arg(f.errorString()));
        return {};
    }
    auto copy = manifest;
    copy["format"] = 1;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(copy)).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        setError(tr("Couldn't save the file: %1").arg(f.errorString()));
        return {};
    }
    return target;
}

QVariantMap SharingBridge::openFile(const QString& path)
{
    QString source = path;
    if (source.startsWith("file:")) {
        source = QUrl(source).toLocalFile();
    }
    if (source.isEmpty()) {
        source = QFileDialog::getOpenFileName(nullptr, tr("Open shared instance"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                              tr("Fashion pack (*.fashpack);;All files (*)"));
        if (source.isEmpty()) {
            return {};
        }
    }
    QFile f(source);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 16 * 1024 * 1024) {
        return { { "error", tr("Couldn't open “%1”.").arg(QFileInfo(source).fileName()) } };
    }
    return parseLink(QString::fromUtf8(f.readAll()));
}

void SharingBridge::copyText(const QString& text) const
{
    QGuiApplication::clipboard()->setText(text);
}

QString SharingBridge::clipboardText() const
{
    return QGuiApplication::clipboard()->text();
}

// ---- applying ---------------------------------------------------------------------------------------------------

bool SharingBridge::applyManifest(const QVariantMap& manifest, const QVariantMap& options)
{
    if (busy()) {
        return false;
    }
    clearError();
    m_result.clear();
    const QString target = options.value("targetInstanceId").toString();
    if (options.value("keepAsIs").toBool()) {
        if (!APPLICATION->instances()->getInstanceById(target)) {
            setError(tr("Pick one of your instances."));
            return false;
        }
        m_result = { { "instanceId", target }, { "added", 0 }, { "upToDate", 0 }, { "failed", QStringList() }, { "local", QStringList() } };
        emit busyChanged();
        emit applied(target);
        return true;
    }
    if (target.isEmpty()) {
        if (manifest.value("mc").toString().isEmpty()) {
            setError(tr("The pack doesn't say which Minecraft version it needs."));
            return false;
        }
        if (!manifest.value("loader").toString().isEmpty() && loaderUid(manifest.value("loader").toString()).isEmpty()) {
            setError(tr("The pack uses a mod loader this launcher doesn't know: %1").arg(manifest.value("loader").toString()));
            return false;
        }
    } else if (!APPLICATION->instances()->getInstanceById(target)) {
        setError(tr("That instance no longer exists."));
        return false;
    } else if (APPLICATION->instances()->getInstanceById(target)->isRunning()) {
        setError(tr("Close the game before adding mods to it."));
        return false;
    }

    m_manifest = filterManifest(manifest, options);
    m_options = options;
    m_targetId = target;
    m_failed.clear();
    m_stage = Stage::Resolving;
    emit busyChanged();
    resolveStep();
    return true;
}

void SharingBridge::resolveStep()
{
    // Links carry only ids: look up download URLs, hashes and file names for everything that lacks them.
    QStringList modrinthIds, flameIds;
    for (const auto& k : kKinds) {
        for (const auto& v : m_manifest.value(k).toList()) {
            const auto m = v.toMap();
            if (!m.value("url").toString().isEmpty() && !m.value("file").toString().isEmpty()) {
                continue;
            }
            const QString vid = m.value("versionId").toString();
            if (vid.isEmpty()) {
                continue;
            }
            if (m.value("platform") == "modrinth") {
                modrinthIds << vid;
            } else if (m.value("platform") == "curseforge") {
                flameIds << vid;
            }
        }
    }
    if (modrinthIds.isEmpty() && flameIds.isEmpty()) {
        createStep();
        return;
    }
    setStatus(tr("Looking up %n file(s)…", nullptr, static_cast<int>(modrinthIds.size() + flameIds.size())));

    auto job = makeShared<NetJob>("Sharing::Resolve", APPLICATION->network());
    QByteArray* mrResponse = nullptr;
    QByteArray* cfResponse = nullptr;
    if (!modrinthIds.isEmpty()) {
        QUrl url(BuildConfig.MODRINTH_PROD_URL + "/versions");
        QUrlQuery q;
        q.addQueryItem("ids", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(modrinthIds)).toJson(QJsonDocument::Compact)));
        url.setQuery(q);
        auto [action, response] = Net::ApiRequest::makeByteArray(url);
        job->addNetAction(action);
        mrResponse = response;
    }
    if (!flameIds.isEmpty()) {
        QJsonArray ids;
        for (const auto& id : flameIds) {
            ids.append(id.toInt());
        }
        auto [action, response] = Net::ApiRequest::makeByteArray(QString(BuildConfig.FLAME_BASE_URL + "/mods/files"),
                                                                 QJsonDocument(QJsonObject{ { "fileIds", ids } }).toJson());
        job->addNetAction(action);
        cfResponse = response;
    }
    m_task = job;
    connect(job.get(), &Task::finished, this, [this, t = job.get(), mrResponse, cfResponse] {
        if (m_task.get() != t) {
            return;
        }
        const bool aborted = t->getState() == Task::State::AbortedByUser;
        m_task.reset();
        if (aborted) {
            finishApply(QString());
            return;
        }
        QHash<QString, QJsonObject> found;  // version/file id -> { url, file, sha1, size, versionNumber, projectId }
        if (mrResponse) {
            for (const auto& v : QJsonDocument::fromJson(*mrResponse).array()) {
                const auto ver = v.toObject();
                const auto files = ver.value("files").toArray();
                QJsonObject file = files.isEmpty() ? QJsonObject() : files.first().toObject();
                for (const auto& f : files) {
                    if (f.toObject().value("primary").toBool()) {
                        file = f.toObject();
                    }
                }
                found.insert(ver.value("id").toString(), QJsonObject{ { "url", file.value("url") },
                                                                      { "file", file.value("filename") },
                                                                      { "sha1", file.value("hashes").toObject().value("sha1") },
                                                                      { "size", file.value("size") },
                                                                      { "versionNumber", ver.value("version_number") } });
            }
        }
        if (cfResponse) {
            for (const auto& v : QJsonDocument::fromJson(*cfResponse).object().value("data").toArray()) {
                const auto file = v.toObject();
                QString sha1;
                for (const auto& h : file.value("hashes").toArray()) {
                    if (h.toObject().value("algo").toInt() == 1) {
                        sha1 = h.toObject().value("value").toString();
                    }
                }
                found.insert(QString::number(file.value("id").toInteger()), QJsonObject{ { "url", file.value("downloadUrl") },
                                                                                        { "file", file.value("fileName") },
                                                                                        { "sha1", sha1 },
                                                                                        { "size", file.value("fileLength") },
                                                                                        { "versionNumber", file.value("displayName") } });
            }
        }
        for (const auto& k : kKinds) {
            QVariantList items;
            for (const auto& v : m_manifest.value(k).toList()) {
                auto m = v.toMap();
                const auto it = found.constFind(m.value("versionId").toString());
                if (it != found.constEnd()) {
                    for (auto f = it->begin(); f != it->end(); ++f) {
                        if (!f.value().isNull() && !(f.value().isString() && f.value().toString().isEmpty())) {
                            m[f.key()] = f.value().toVariant();
                        }
                    }
                }
                items.append(m);
            }
            m_manifest[k] = items;
        }
        createStep();
    });
    job->start();
}

void SharingBridge::createStep()
{
    if (!m_targetId.isEmpty()) {
        downloadStep();
        return;
    }
    m_stage = Stage::Creating;
    const QString mc = m_manifest.value("mc").toString();
    const QString loader = m_manifest.value("loader").toString();
    auto* index = APPLICATION->metadataIndex();
    InstanceTask* raw = nullptr;
    if (loader.isEmpty()) {
        raw = new VanillaCreationTask(index->get("net.minecraft", mc));
    } else {
        const QString uid = loaderUid(loader);
        raw = new VanillaCreationTask(index->get("net.minecraft", mc), uid, index->get(uid, m_manifest.value("loaderVersion").toString()));
    }
    QString name = m_options.value("name").toString().trimmed();
    if (name.isEmpty()) {
        name = m_manifest.value("name").toString().trimmed();
    }
    if (name.isEmpty()) {
        name = tr("Shared pack");
    }
    raw->setName(name);
    raw->setOriginalName(name, QString());
    raw->setGroup(QString());
    raw->setIcon("default");
    setStatus(tr("Creating “%1”…").arg(name));

    Task::Ptr task(APPLICATION->instances()->wrapInstanceTask(raw));
    m_task = task;
    auto selectConn = std::make_shared<QMetaObject::Connection>();
    *selectConn = connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this,
                          [this](const QString& id) { m_targetId = id; });
    connect(task.get(), &Task::status, this, [this](const QString& s) { setStatus(s, m_progress); });
    connect(task.get(), &Task::progress, this, [this](qint64 current, qint64 total) {
        setStatus(m_status, total > 0 ? qBound(0.0, static_cast<double>(current) / static_cast<double>(total), 1.0) * 0.3 : -1);
    });
    connect(task.get(), &Task::finished, this, [this, t = task.get(), selectConn] {
        disconnect(*selectConn);
        if (m_task.get() != t) {
            return;
        }
        const bool ok = t->wasSuccessful();
        const bool aborted = t->getState() == Task::State::AbortedByUser;
        const QString reason = t->failReason();
        m_task.reset();
        if (!ok || m_targetId.isEmpty()) {
            finishApply(aborted ? QString() : (reason.isEmpty() ? tr("Creating the instance failed.") : reason));
            return;
        }
        downloadStep();
    });
    task->start();
}

void SharingBridge::downloadStep()
{
    m_stage = Stage::Downloading;
    auto* inst = APPLICATION->instances()->getInstanceById(m_targetId);
    if (!inst) {
        finishApply(tr("That instance no longer exists."));
        return;
    }
    auto* profile = inst->getPackProfile();
    if (!profile->getComponent("net.minecraft")) {
        profile->reload(Net::Mode::Offline);
    }
    const auto existing = scanInstance(m_targetId, false);

    auto all = makeShared<ConcurrentTask>(tr("Downloading mods"), concurrentDownloads());
    int queued = 0, upToDate = 0;
    QStringList local;
    auto oldFiles = std::make_shared<QStringList>();  // replaced files without metadata, removed after success
    for (const auto& k : kKinds) {
        ResourceFolderModel* model = k == "mods"            ? static_cast<ResourceFolderModel*>(inst->loaderModList())
                                     : k == "resourcepacks" ? static_cast<ResourceFolderModel*>(inst->resourcePackList())
                                                            : static_cast<ResourceFolderModel*>(inst->shaderPackList());
        QHash<QString, QVariantMap> have;
        QSet<QString> haveHashes, haveFiles;
        for (const auto& v : existing.value(k).toList()) {
            const auto m = v.toMap();
            have.insert(m.value("key").toString(), m);
            haveFiles.insert(m.value("file").toString());
            if (!m.value("sha1").toString().isEmpty()) {
                haveHashes.insert(m.value("sha1").toString());
            }
        }
        for (const auto& v : m_manifest.value(k).toList()) {
            const auto m = v.toMap();
            const QString key = m.value("key").toString();
            const auto it = have.constFind(key);
            const bool present = (it != have.constEnd() && it->value("versionId") == m.value("versionId") && !m.value("versionId").toString().isEmpty()) ||
                                 (!m.value("sha1").toString().isEmpty() && haveHashes.contains(m.value("sha1").toString())) ||
                                 (haveFiles.contains(m.value("file").toString()) && !m.value("file").toString().isEmpty());
            if (present) {
                ++upToDate;
                continue;
            }
            if (m.value("platform") == "local") {
                local << m.value("name").toString();
                continue;
            }
            if (m.value("url").toString().isEmpty()) {
                m_failed << tr("%1 (the author doesn't allow automatic downloads)").arg(m.value("name").toString());
                continue;
            }
            auto pack = std::make_shared<ModPlatform::IndexedPack>();
            pack->provider = m.value("platform") == "curseforge" ? ModPlatform::ResourceProvider::FLAME : ModPlatform::ResourceProvider::MODRINTH;
            pack->addonId = m.value("projectId");
            pack->name = m.value("name").toString();
            pack->slug = m.value("slug").toString().isEmpty() ? slugify(pack->name) : m.value("slug").toString();
            pack->side = sideType(m.value("side").toString());
            ModPlatform::IndexedVersion ver;
            ver.addonId = pack->addonId;
            ver.fileId = m.value("versionId");
            ver.downloadUrl = m.value("url").toString();
            ver.fileName = m.value("file").toString();
            ver.version = m.value("versionNumber").toString();
            ver.versionNumber = m.value("versionNumber").toString();
            ver.mcVersion = { m_manifest.value("mc").toString() };
            if (!m.value("sha1").toString().isEmpty()) {
                ver.hashType = "sha1";
                ver.hash = m.value("sha1").toString();
            }
            ver.side = pack->side;
            if (ver.fileName.isEmpty()) {
                ver.fileName = QUrl(ver.downloadUrl).fileName();
            }
            if (it != have.constEnd() && it->value("platform") == "local") {
                // A different version of a mod the player added by hand: swap it once the new one is in.
                *oldFiles << QDir(model->dir()).absoluteFilePath(it->value("file").toString());
            }
            auto dl = makeShared<ResourceDownloadTask>(pack, ver, model);
            const QString name = pack->name;
            connect(dl.get(), &Task::failed, this, [this, name](const QString& reason) {
                m_failed << QString("%1 (%2)").arg(name, reason.section('\n', 0, 0));
            });
            all->addTask(dl);
            ++queued;
        }
    }
    m_result = { { "instanceId", m_targetId }, { "added", 0 }, { "upToDate", upToDate }, { "local", local } };
    if (queued == 0) {
        finishApply(QString());
        return;
    }
    setStatus(tr("Downloading %n file(s)…", nullptr, queued), 0.3);
    m_task = all;
    connect(all.get(), &Task::progress, this, [this](qint64 current, qint64 total) {
        setStatus(m_status, total > 0 ? 0.3 + 0.7 * qBound(0.0, static_cast<double>(current) / static_cast<double>(total), 1.0) : -1);
    });
    connect(all.get(), &Task::finished, this, [this, t = all.get(), queued, oldFiles] {
        if (m_task.get() != t) {
            return;
        }
        const bool aborted = t->getState() == Task::State::AbortedByUser;
        m_task.reset();
        for (const auto& f : *oldFiles) {
            if (QFileInfo::exists(f)) {
                FS::deletePath(f);
            }
        }
        m_result["added"] = queued - static_cast<int>(m_failed.size());
        finishApply(aborted ? tr("Stopped. Files downloaded so far were kept.") : QString());
    });
    all->start();
}

void SharingBridge::finishApply(const QString& error)
{
    const QString id = m_targetId;
    m_stage = Stage::Idle;
    m_task.reset();
    m_status.clear();
    m_progress = -1;
    m_result["failed"] = m_failed;
    if (!m_result.contains("instanceId")) {
        m_result["instanceId"] = id;
    }
    emit progressChanged();
    emit busyChanged();
    if (!error.isEmpty()) {
        qWarning() << "[share] apply failed:" << error;
        setError(error);
        return;
    }
    if (id.isEmpty()) {
        return;  // cancelled before the instance existed
    }
    qInfo() << "[share] applied to" << id << m_result;
    if (auto* inst = APPLICATION->instances()->getInstanceById(id)) {
        inst->loaderModList()->update();
    }
    emit applied(id);
}

void SharingBridge::cancel()
{
    if (m_task && m_task->canAbort()) {
        m_task->abort();
    }
}

void SharingBridge::setError(const QString& e)
{
    m_error = e;
    emit errorChanged();
}

void SharingBridge::clearError()
{
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
}

void SharingBridge::setStatus(const QString& s, double p)
{
    m_status = s;
    m_progress = p;
    emit progressChanged();
}

// ---- automation (FFPRISM_SHARE) ---------------------------------------------------------------------------------
// link:<instanceId>[:server]      print the share link and quit
// apply:<file with link>:<target> apply it to an instance id, or "new", then quit
void SharingBridge::runAutoTest()
{
    const QString spec = qEnvironmentVariable("FFPRISM_SHARE");
    const QStringList parts = spec.split(':');
    const auto quit = [](int code) { QTimer::singleShot(500, qApp, [code] { QCoreApplication::exit(code); }); };
    if (parts.value(0) == "link") {
        const QString id = parts.value(1);
        const bool includeClient = parts.value(2) != "server";
        connect(this, &SharingBridge::analyzed, this, [this, id, includeClient, quit] {
            const auto manifest = createManifest(id, { { "includeClient", includeClient } });
            qInfo().noquote() << "[share] manifest" << QJsonDocument(QJsonObject::fromVariantMap(manifest)).toJson(QJsonDocument::Compact);
            qInfo().noquote() << "[share] link" << packLink(manifest);
            quit(0);
        });
        analyze(id);
    } else if (parts.value(0) == "apply") {
        const auto manifest = openFile(parts.value(1));
        if (manifest.contains("error") || manifest.isEmpty()) {
            qWarning() << "[share] bad input" << manifest;
            quit(1);
            return;
        }
        const QString target = parts.value(2) == "new" ? QString() : parts.value(2);
        qInfo() << "[share] matches" << matchInstances(manifest);
        connect(this, &SharingBridge::applied, this, [quit](const QString&) { quit(0); });
        connect(this, &SharingBridge::errorChanged, this, [this, quit] {
            if (!m_error.isEmpty()) {
                qWarning() << "[share] error" << m_error;
                quit(1);
            }
        });
        if (!applyManifest(manifest, { { "targetInstanceId", target }, { "includeClient", true } })) {
            quit(1);
        }
    }
}
