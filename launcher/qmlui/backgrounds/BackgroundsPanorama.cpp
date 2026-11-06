// SPDX-License-Identifier: GPL-3.0-only
// Title-screen panorama for BackgroundsBridge: where the six faces come from, importing your own,
// and installing a panorama-screenshot mod for "Capture from my world".
#include "BackgroundsBridge.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>
#include <QtConcurrent>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "PanoramaTools.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/LaunchProfile.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

namespace {

struct Candidate {
    enum Kind { CapturedDir, Pack, Assets, Jar } kind;
    QString path;
    QString origin;
};

struct Found {
    QStringList faces;  // file URLs
    QString origin;
};

QStringList faceUrls(const QString& dir)
{
    QStringList urls;
    for (int i = 0; i < 6; i++) {
        urls << QUrl::fromLocalFile(QDir(dir).filePath(QString("panorama_%1.png").arg(i))).toString();
    }
    return urls;
}

QString stamp(const QString& path)
{
    const QFileInfo fi(path);
    return path + "|" + QString::number(fi.size()) + "|" + QString::number(fi.lastModified().toSecsSinceEpoch());
}

/// Packs enabled in options.txt, highest priority first; every pack in the folder when there is no options.txt.
QStringList enabledPacks(const QString& gameRoot)
{
    const QString packDir = FS::PathCombine(gameRoot, "resourcepacks");
    QStringList packs;
    QFile options(FS::PathCombine(gameRoot, "options.txt"));
    if (options.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!options.atEnd()) {
            const QString line = QString::fromUtf8(options.readLine()).trimmed();
            if (!line.startsWith("resourcePacks:")) {
                continue;
            }
            const auto list = QJsonDocument::fromJson(line.mid(14).toUtf8()).array();
            for (int i = list.size() - 1; i >= 0; i--) {
                const QString id = list.at(i).toString();
                if (id.startsWith("file/")) {
                    packs << FS::PathCombine(packDir, id.mid(5));
                }
            }
            return packs;
        }
        return packs;
    }
    const auto entries = QDir(packDir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& fi : entries) {
        if (!fi.fileName().endsWith(".disabled")) {
            packs << fi.absoluteFilePath();
        }
    }
    return packs;
}

QString clientJar(const QString& version)
{
    return FS::PathCombine(APPLICATION->dataRoot(), "libraries/com/mojang/minecraft", version,
                           QString("minecraft-%1-client.jar").arg(version));
}

/// Newest client jar in the launcher's libraries cache — the panorama for instances that never ran.
QString anyClientJar()
{
    QString best;
    QDateTime bestTime;
    QDirIterator it(FS::PathCombine(APPLICATION->dataRoot(), "libraries/com/mojang/minecraft"), { "minecraft-*-client.jar" },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi(it.next());
        if (bestTime.isNull() || fi.lastModified() > bestTime) {
            best = fi.absoluteFilePath();
            bestTime = fi.lastModified();
        }
    }
    return best;
}

QString loaderOf(MinecraftInstance* mc)
{
    auto* p = mc->getPackProfile();
    if (!p->getComponentVersion("org.quiltmc.quilt-loader").isEmpty())
        return "quilt";
    if (!p->getComponentVersion("net.fabricmc.fabric-loader").isEmpty())
        return "fabric";
    if (!p->getComponentVersion("net.neoforged").isEmpty())
        return "neoforge";
    if (!p->getComponentVersion("net.minecraftforge").isEmpty())
        return "forge";
    return {};
}

// Mods that take Minecraft-style panorama screenshots, best first. All of them press F4 by default.
const QStringList kPanoramaMods = { "panorama-screenshot", "panorama_screen", "swd-panorama", "panoramica" };

}  // namespace

void BackgroundsBridge::setPanoramaSource(const QString& source)
{
    const QString value = source.isEmpty() ? "auto" : source;
    if (value == m_panoramaSource) {
        return;
    }
    m_panoramaSource = value;
    APPLICATION->settings()->set("QmlPanoramaSource", value);
    emit panoramaSourceChanged();
}

void BackgroundsBridge::scanCustomPanoramas()
{
    QVariantList list;
    const auto dirs = QDir(m_panoramaDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const auto& d : dirs) {
        if (PanoramaTools::hasFaces(d.absoluteFilePath())) {
            list.append(QVariantMap{ { "name", d.fileName() }, { "thumb", faceUrls(d.absoluteFilePath()).first() } });
        }
    }
    if (list != m_customPanoramas) {
        m_customPanoramas = list;
        emit customPanoramasChanged();
    }
}

void BackgroundsBridge::loadPanorama(const QString& instanceId)
{
    const int request = ++m_panoramaRequest;

    // The user's own panorama: already normalized when it was added.
    if (m_panoramaSource != "auto") {
        const QString dir = FS::PathCombine(m_panoramaDir, m_panoramaSource);
        if (PanoramaTools::hasFaces(dir)) {
            m_panorama = QVariantMap{ { "faces", faceUrls(dir) }, { "origin", tr("Your panorama") }, { "busy", false } };
            emit panoramaChanged();
            return;
        }
    }

    // Automatic: what this instance would show on its own title screen.
    QList<Candidate> candidates;
    if (auto* mc = APPLICATION->instances()->getInstanceById(instanceId)) {
        const QString root = mc->gameRoot();
        candidates.append({ Candidate::CapturedDir, root, tr("Captured in this instance") });
        for (const auto& pack : enabledPacks(root)) {
            candidates.append({ Candidate::Pack, pack, tr("From the resource pack “%1”").arg(QFileInfo(pack).completeBaseName()) });
        }
        const QString version = mc->getPackProfile()->getComponentVersion("net.minecraft");
        if (auto profile = mc->getPackProfile()->getProfile()) {
            if (auto assets = profile->getMinecraftAssets(); assets && !assets->id.isEmpty()) {
                candidates.append({ Candidate::Assets, FS::PathCombine(APPLICATION->dataRoot(), "assets/indexes", assets->id + ".json"),
                                    tr("Minecraft %1").arg(version) });
            }
        }
        if (!version.isEmpty()) {
            candidates.append({ Candidate::Jar, clientJar(version), tr("Minecraft %1").arg(version) });
        }
    }
    // Any downloaded game version, newest first, for instances that never ran.
    const auto indexes = QDir(FS::PathCombine(APPLICATION->dataRoot(), "assets/indexes")).entryInfoList({ "*.json" }, QDir::Files, QDir::Time);
    for (const auto& fi : indexes) {
        candidates.append({ Candidate::Assets, fi.absoluteFilePath(), tr("Minecraft") });
    }
    if (const QString any = anyClientJar(); !any.isEmpty()) {
        const QString v = QFileInfo(any).dir().dirName();
        candidates.append({ Candidate::Jar, any, tr("Minecraft %1").arg(v) });
    }

    m_panorama["busy"] = true;
    emit panoramaChanged();

    const QString cacheRoot = FS::PathCombine(m_cacheDir, "panoramas");
    const QString objectsDir = FS::PathCombine(APPLICATION->dataRoot(), "assets/objects");
    QPointer<BackgroundsBridge> self(this);
    auto future = QtConcurrent::run([candidates, cacheRoot, objectsDir]() -> Found {
        for (const auto& c : candidates) {
            QString source = c.path;
            if (c.kind == Candidate::CapturedDir) {
                source = PanoramaTools::newestPanoramaDir({ FS::PathCombine(c.path, "screenshots"), FS::PathCombine(c.path, "panoramas") });
                if (source.isEmpty()) {
                    continue;
                }
                source = FS::PathCombine(source, "panorama_5.png");  // stamp the newest face
            } else if (!QFileInfo::exists(source)) {
                continue;
            }
            const QString key = QCryptographicHash::hash(stamp(source).toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
            const QString dir = FS::PathCombine(cacheRoot, key);
            if (PanoramaTools::hasFaces(dir)) {
                return { faceUrls(dir), c.origin };
            }
            const auto faces = c.kind == Candidate::CapturedDir ? PanoramaTools::facesFromDir(QFileInfo(source).absolutePath())
                               : c.kind == Candidate::Assets    ? PanoramaTools::facesFromAssetIndex(source, objectsDir)
                                                                : PanoramaTools::facesFromPack(source);
            if (!faces.isEmpty() && PanoramaTools::saveFaces(faces, dir)) {
                return { faceUrls(dir), c.origin };
            }
        }
        return {};
    });
    auto* watcher = new QFutureWatcher<Found>(this);
    connect(watcher, &QFutureWatcher<Found>::finished, this, [self, watcher, request] {
        watcher->deleteLater();
        if (!self || request != self->m_panoramaRequest) {
            return;  // a newer request superseded this one
        }
        const Found f = watcher->result();
        self->m_panorama = QVariantMap{ { "faces", f.faces }, { "origin", f.origin }, { "busy", false } };
        emit self->panoramaChanged();
    });
    watcher->setFuture(future);
}

void BackgroundsBridge::importPanorama(const QList<QUrl>& urls)
{
    QStringList files;
    for (const auto& u : urls) {
        files << u.toLocalFile();
    }
    if (files.isEmpty()) {
        return;
    }
    // Readable folder name from the first file.
    QString base = QFileInfo(files.first()).completeBaseName().toLower();
    if (files.size() == 6) {
        base = QFileInfo(files.first()).dir().dirName().toLower();
    }
    base.replace(QRegularExpression("[^a-z0-9_-]+"), "-");
    base = base.left(40);
    if (base.isEmpty() || base == "-") {
        base = "panorama";
    }
    QString name = base;
    for (int i = 2; QFileInfo::exists(FS::PathCombine(m_panoramaDir, name)); i++) {
        name = QString("%1-%2").arg(base).arg(i);
    }
    const QString target = FS::PathCombine(m_panoramaDir, name);

    const QString errWrongFiles = tr("Pick a resource pack, six face pictures or one 360° picture (twice as wide as it is tall).");
    const QString errNoPanorama = tr("That resource pack has no panorama.");
    const QString errSave = tr("Couldn't save the panorama.");

    auto future = QtConcurrent::run([files, target, errWrongFiles, errNoPanorama, errSave]() -> QString {
        QList<QImage> faces;
        if (files.size() == 6) {
            faces = PanoramaTools::facesFromFiles(files);
        } else if (files.size() == 1) {
            const QFileInfo fi(files.first());
            const QString ext = fi.suffix().toLower();
            if (fi.isDir() || ext == "zip" || ext == "jar") {
                faces = PanoramaTools::facesFromPack(fi.absoluteFilePath());
                if (faces.isEmpty() && fi.isDir()) {
                    faces = PanoramaTools::facesFromDir(fi.absoluteFilePath());
                }
                if (faces.isEmpty()) {
                    return errNoPanorama;
                }
            } else {
                QImage img(fi.absoluteFilePath());
                if (img.isNull() || std::abs(double(img.width()) / std::max(1, img.height()) - 2.0) > 0.1) {
                    return errWrongFiles;
                }
                // Huge 360° photos (8K+) are scaled first: faces end up 1024 px anyway.
                if (img.width() > 4 * PanoramaTools::kMaxFace) {
                    img = img.scaledToWidth(4 * PanoramaTools::kMaxFace, Qt::SmoothTransformation);
                }
                faces = PanoramaTools::facesFromEquirect(img);
            }
        }
        if (faces.isEmpty()) {
            return errWrongFiles;
        }
        if (!PanoramaTools::saveFaces(faces, target)) {
            QDir(target).removeRecursively();
            return errSave;
        }
        return {};
    });
    QPointer<BackgroundsBridge> self(this);
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [self, watcher, name] {
        watcher->deleteLater();
        if (!self) {
            return;
        }
        const QString error = watcher->result();
        if (error.isEmpty()) {
            self->scanCustomPanoramas();
            self->setPanoramaSource(name);
        }
        emit self->panoramaImported(error.isEmpty() ? name : QString(), error);
    });
    watcher->setFuture(future);
}

bool BackgroundsBridge::removePanorama(const QString& name)
{
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.startsWith('.')) {
        return false;
    }
    const bool ok = QDir(FS::PathCombine(m_panoramaDir, name)).removeRecursively();
    if (m_panoramaSource == name) {
        setPanoramaSource("auto");
    }
    scanCustomPanoramas();
    return ok;
}

// ---- Capture from my world ----

void BackgroundsBridge::setCapture(const QString& instanceId, const QString& state, const QString& message)
{
    m_capture = QVariantMap{ { "instanceId", instanceId }, { "state", state }, { "message", message } };
    emit captureChanged();
}

QNetworkReply* BackgroundsBridge::apiGet(const QString& url, bool curseforge)
{
    QNetworkRequest req{ QUrl(url) };
    req.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (curseforge) {
        req.setRawHeader("x-api-key", APPLICATION->getFlameAPIKey().toUtf8());
    }
    req.setTransferTimeout(30000);
    return m_net->get(req);
}

static QString versionsUrl(const QString& project, const QStringList& loaders, const QString& mcVersion)
{
    QUrlQuery q;
    q.addQueryItem("loaders", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(loaders)).toJson(QJsonDocument::Compact)));
    q.addQueryItem("game_versions", QString::fromUtf8(QJsonDocument(QJsonArray{ mcVersion }).toJson(QJsonDocument::Compact)));
    QUrl url("https://api.modrinth.com/v2/project/" + project + "/version");
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

void BackgroundsBridge::installPanoramaMod(const QString& instanceId)
{
    auto* mc = APPLICATION->instances()->getInstanceById(instanceId);
    if (!mc) {
        setCapture(instanceId, "error", tr("Pick a Minecraft instance first."));
        return;
    }
    const QString loader = loaderOf(mc);
    const QString mcVersion = mc->getPackProfile()->getComponentVersion("net.minecraft");
    if (loader.isEmpty()) {
        setCapture(instanceId, "error",
                   tr("Panorama mods need a mod loader. Add Fabric to this instance (Instance → Version), then try again."));
        return;
    }
    const QStringList loaders = loader == "quilt" ? QStringList{ "quilt", "fabric" } : QStringList{ loader };
    const QString modsDir = FS::PathCombine(mc->gameRoot(), "mods");
    setCapture(instanceId, "working", tr("Looking for a panorama mod for %1 %2…").arg(loader, mcVersion));

    // Try the mods one after another until one has a build for this loader and version.
    auto tryNext = std::make_shared<std::function<void(int)>>();
    QPointer<BackgroundsBridge> self(this);
    *tryNext = [self, tryNext, instanceId, loaders, loader, mcVersion, modsDir](int i) {
        if (!self) {
            return;
        }
        if (i >= kPanoramaMods.size()) {
            self->setCapture(instanceId, "error",
                             tr("No panorama mod works with %1 %2 yet. You can still add your own panorama above.").arg(loader, mcVersion));
            *tryNext = nullptr;  // break the self-reference
            return;
        }
        auto* reply = self->apiGet(versionsUrl(kPanoramaMods[i], loaders, mcVersion));
        connect(reply, &QNetworkReply::finished, self, [self, reply, tryNext, i, instanceId, loaders, mcVersion, modsDir] {
            reply->deleteLater();
            const auto versions = QJsonDocument::fromJson(reply->readAll()).array();
            if (reply->error() != QNetworkReply::NoError && reply->error() != QNetworkReply::ContentNotFoundError) {
                self->setCapture(instanceId, "error", tr("Couldn't reach Modrinth. Check your connection and try again."));
                *tryNext = nullptr;
                return;
            }
            if (versions.isEmpty()) {
                (*tryNext)(i + 1);
                return;
            }
            *tryNext = nullptr;
            self->installModVersion(instanceId, modsDir, versions.first().toObject().toVariantMap(), kPanoramaMods[i], loaders, mcVersion);
        });
    };
    (*tryNext)(0);
}

void BackgroundsBridge::installModVersion(const QString& instanceId,
                                          const QString& modsDir,
                                          const QVariantMap& version,
                                          const QString& slug,
                                          const QStringList& loaders,
                                          const QString& mcVersion)
{
    QDir().mkpath(modsDir);
    // Everything to download: the mod plus its required dependencies that aren't there yet.
    struct Job {
        int pending = 0;
        bool failed = false;
    };
    auto job = std::make_shared<Job>();
    QPointer<BackgroundsBridge> self(this);

    const QString modName = version.value("name").toString().isEmpty() ? slug : version.value("name").toString();
    auto finishOne = [self, job, instanceId, slug]() {
        if (!self || --job->pending > 0) {
            return;
        }
        if (job->failed) {
            self->setCapture(instanceId, "error", tr("The download failed. Try again in a moment."));
            return;
        }
        const QString how = slug == "panoramica"
                                ? tr("press the panorama key (Options → Controls → Panoramica)")
                                : tr("press F4 (you can change the key in Options → Controls)");
        self->setCapture(instanceId, "done",
                         tr("Panorama mod installed. Start the game, go to a nice spot, look straight ahead and %1. Keep still for a "
                            "second while it takes six pictures. After you close the game, the panorama shows up here on its own.")
                             .arg(how));
    };

    auto download = [self, job, modsDir, finishOne](const QVariantMap& v) {
        QVariantMap file;
        for (const auto& f : v.value("files").toList()) {
            if (file.isEmpty() || f.toMap().value("primary").toBool()) {
                file = f.toMap();
            }
        }
        const QString name = QFileInfo(file.value("filename").toString()).fileName();
        if (name.isEmpty()) {
            job->failed = true;
            finishOne();
            return;
        }
        const QString path = FS::PathCombine(modsDir, name);
        if (QFileInfo::exists(path)) {
            finishOne();
            return;
        }
        const QByteArray sha1 = file.value("hashes").toMap().value("sha1").toByteArray();
        auto* reply = self->apiGet(file.value("url").toString());
        connect(reply, &QNetworkReply::finished, self, [reply, path, sha1, job, finishOne] {
            reply->deleteLater();
            const QByteArray data = reply->readAll();
            QSaveFile out(path);
            if (reply->error() != QNetworkReply::NoError ||
                (!sha1.isEmpty() && QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex() != sha1) ||
                !out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
                job->failed = true;
            }
            finishOne();
        });
    };

    job->pending = 1;
    const auto existing = QDir(modsDir).entryList(QDir::Files);
    for (const auto& d : version.value("dependencies").toList()) {
        const auto dep = d.toMap();
        const QString project = dep.value("project_id").toString();
        if (dep.value("dependency_type").toString() != "required" || project.isEmpty()) {
            continue;
        }
        job->pending++;
        // Look the dependency up (slug to see whether it's installed, then a build for this version).
        auto* info = apiGet("https://api.modrinth.com/v2/project/" + project);
        connect(info, &QNetworkReply::finished, this, [self, info, project, existing, loaders, mcVersion, job, download, finishOne] {
            info->deleteLater();
            const QString depSlug = QJsonDocument::fromJson(info->readAll()).object().value("slug").toString();
            if (!depSlug.isEmpty() &&
                std::any_of(existing.begin(), existing.end(), [&](const QString& f) { return f.toLower().startsWith(depSlug.toLower()); })) {
                finishOne();
                return;
            }
            auto* vr = self->apiGet(versionsUrl(project, loaders, mcVersion));
            connect(vr, &QNetworkReply::finished, self, [vr, job, download, finishOne] {
                vr->deleteLater();
                const auto list = QJsonDocument::fromJson(vr->readAll()).array();
                if (list.isEmpty()) {
                    job->failed = true;
                    finishOne();
                    return;
                }
                download(list.first().toObject().toVariantMap());
            });
        });
    }
    setCapture(instanceId, "working", tr("Installing %1…").arg(modName));
    download(version);
}
