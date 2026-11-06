// SPDX-License-Identifier: GPL-3.0-only
#include "GalleryBridge.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <cmath>

#include "Application.h"
#include "FileSystem.h"
#include "settings/SettingsObject.h"

namespace {
const QString kIndexSetting = QStringLiteral("QmlGalleryIndexUrl");
const QString kSoundsOnSetting = QStringLiteral("QmlUiSounds");
const QString kSoundVolumeSetting = QStringLiteral("QmlUiSoundVolume");
const QString kDefaultIndex = QStringLiteral("https://raw.githubusercontent.com/fashffffffff/fashion-gallery/main/index.json");
const QString kSubmitUrl = QStringLiteral("https://github.com/fashffffffff/fashion-gallery");

constexpr qint64 kMaxIndex = 2 * 1024 * 1024;
constexpr qint64 kMaxTheme = 64 * 1024;
constexpr qint64 kMaxBackground = 40 * 1024 * 1024;
constexpr int kTimeoutMs = 10000;

const QStringList kImageExt = { "png", "jpg", "jpeg", "webp" };
const QStringList kVideoExt = { "mp4", "webm", "mov", "m4v" };

QString clean(const QVariant& v, int maxLen)
{
    return v.toString().simplified().left(maxLen);
}

QString safeId(const QString& id)
{
    QString s = id.toLower();
    s.replace(QRegularExpression("[^a-z0-9_-]+"), "-");
    return s.left(64);
}

// Base theme from the resources: defines which keys a theme may have and what type each is.
const QJsonObject& baseTheme()
{
    static const QJsonObject base = [] {
        QFile f(":/qmlui/themes/base.json");
        f.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return base;
}

bool plainFileName(const QString& s)
{
    return !s.contains('/') && !s.contains('\\') && !s.startsWith('.') && !s.contains(':') && s.size() <= 128;
}
}  // namespace

GalleryBridge::GalleryBridge(QObject* parent) : QObject(parent)
{
    auto s = APPLICATION->settings();
    if (!s->contains(kIndexSetting)) {
        s->registerSetting(kIndexSetting, kDefaultIndex);
    }
    if (!s->contains(kSoundsOnSetting)) {
        s->registerSetting(kSoundsOnSetting, true);
    }
    if (!s->contains(kSoundVolumeSetting)) {
        s->registerSetting(kSoundVolumeSetting, 0.5);
    }
    m_themesDir = FS::PathCombine(APPLICATION->dataRoot(), "ui-themes");
    loadInstalled();
}

// ---------------------------------------------------------------- settings

QString GalleryBridge::indexUrl() const
{
    return APPLICATION->settings()->get(kIndexSetting).toString();
}

void GalleryBridge::setIndexUrl(const QString& url)
{
    const QString value = url.trimmed().isEmpty() ? kDefaultIndex : url.trimmed();
    if (value == indexUrl()) {
        return;
    }
    APPLICATION->settings()->set(kIndexSetting, value);
    emit indexUrlChanged();
    refresh();
}

QString GalleryBridge::defaultIndexUrl() const
{
    return kDefaultIndex;
}

QString GalleryBridge::submitUrl() const
{
    return kSubmitUrl;
}

bool GalleryBridge::soundsOn() const
{
    return APPLICATION->settings()->get(kSoundsOnSetting).toBool();
}

void GalleryBridge::setSoundsOn(bool on)
{
    if (on == soundsOn()) {
        return;
    }
    APPLICATION->settings()->set(kSoundsOnSetting, on);
    emit soundsChanged();
}

qreal GalleryBridge::soundVolume() const
{
    return qBound(0.0, APPLICATION->settings()->get(kSoundVolumeSetting).toDouble(), 1.0);
}

void GalleryBridge::setSoundVolume(qreal v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(v + 1, soundVolume() + 1)) {
        return;
    }
    APPLICATION->settings()->set(kSoundVolumeSetting, v);
    emit soundsChanged();
}

// ---------------------------------------------------------------- downloads

void GalleryBridge::fetch(const QUrl& url, qint64 maxBytes, Done done)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == "file") {
        // local sample gallery: read directly
        QFile f(url.toLocalFile());
        if (!f.open(QIODevice::ReadOnly)) {
            done({}, tr("File not found: %1").arg(url.toLocalFile()));
        } else if (f.size() > maxBytes) {
            done({}, tr("The file is too big."));
        } else {
            done(f.readAll(), {});
        }
        return;
    }
    if (scheme != "https" && scheme != "http") {
        done({}, tr("Unsupported address: %1").arg(url.toString()));
        return;
    }
    QNetworkRequest req(url);
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = APPLICATION->network()->get(req);
    auto tooBig = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes, tooBig](qint64 received, qint64 total) {
        if (received > maxBytes || total > maxBytes) {
            *tooBig = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, done, tooBig] {
        reply->deleteLater();
        if (*tooBig) {
            done({}, tr("The file is too big."));
        } else if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->errorString());
        } else {
            done(reply->readAll(), {});
        }
    });
}

// ---------------------------------------------------------------- index

QUrl GalleryBridge::sampleIndexUrl() const
{
    // The sample gallery ships next to the launcher; in a dev build it is found in the source tree.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        FS::PathCombine(APPLICATION->dataRoot(), "gallery"),
        FS::PathCombine(appDir, "gallery"),
        FS::PathCombine(appDir, "../Resources/gallery"),
        FS::PathCombine(appDir, "../share/fashionlauncher/gallery"),
        QFileInfo(QStringLiteral(__FILE__)).absolutePath() + "/../../../gallery",
    };
    for (const auto& dir : candidates) {
        const QFileInfo index(FS::PathCombine(dir, "index.json"));
        if (index.isFile()) {
            return QUrl::fromLocalFile(index.canonicalFilePath());
        }
    }
    return {};
}

void GalleryBridge::refresh()
{
    const int gen = ++m_generation;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    const QUrl url = QUrl::fromUserInput(indexUrl());
    fetch(url, kMaxIndex, [this, gen, url](const QByteArray& data, const QString& error) {
        if (gen != m_generation) {
            return;
        }
        QString parseError = error;
        if (error.isEmpty() && loadIndex(data, url, &parseError)) {
            m_usingSample = false;
        } else if (loadSample()) {
            m_usingSample = true;
            qWarning() << "Gallery: index" << url << "unavailable (" << parseError << "), using the bundled sample";
        } else {
            m_items.clear();
            m_error = parseError;
            emit itemsChanged();
        }
        m_loading = false;
        emit stateChanged();
        recheckInstalled();
        fillThemeColors();
        if (!m_autoStarted) {
            m_autoStarted = true;
            m_autoQueue = qEnvironmentVariable("FFPRISM_GALLERY_AUTOINSTALL").split(',', Qt::SkipEmptyParts);
            autoInstallNext();
        }
    });
}

void GalleryBridge::autoInstallNext()
{
    if (m_autoQueue.isEmpty()) {
        return;
    }
    // give QML time to handle the previous installFinished (backgrounds are imported there)
    QTimer::singleShot(500, this, [this] {
        if (!m_busyId.isEmpty()) {
            autoInstallNext();
            return;
        }
        const QString id = m_autoQueue.takeFirst();
        qInfo() << "Gallery: auto-installing" << id;
        install(id);
        autoInstallNext();
    });
}

bool GalleryBridge::loadSample()
{
    const QUrl url = sampleIndexUrl();
    if (url.isEmpty()) {
        return false;
    }
    QFile f(url.toLocalFile());
    QString error;
    return f.open(QIODevice::ReadOnly) && f.size() <= kMaxIndex && loadIndex(f.readAll(), url, &error);
}

bool GalleryBridge::loadIndex(const QByteArray& data, const QUrl& base, QString* error)
{
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        *error = tr("The gallery list is damaged: %1").arg(pe.errorString());
        return false;
    }
    const QJsonArray arr = doc.isArray() ? doc.array() : doc.object().value("items").toArray();
    const bool localBase = base.isLocalFile();
    // Remote lists may only point at web addresses; only the local sample may use local files.
    auto resolve = [&](const QString& ref) -> QString {
        if (ref.isEmpty()) {
            return {};
        }
        const QUrl u = base.resolved(QUrl(ref));
        const QString scheme = u.scheme().toLower();
        if (scheme == "https" || scheme == "http" || (localBase && scheme == "file")) {
            return u.toString();
        }
        return {};
    };

    QVariantList list;
    QSet<QString> seen;
    for (const auto& value : arr) {
        const QVariantMap in = value.toObject().toVariantMap();
        const QString id = safeId(in.value("id").toString());
        const QString type = in.value("type").toString();
        const QString file = resolve(in.value("file").toString());
        if (id.isEmpty() || seen.contains(id) || (type != "theme" && type != "background") || file.isEmpty()) {
            continue;
        }
        seen.insert(id);
        QStringList tags;
        for (const auto& t : in.value("tags").toList()) {
            const QString tag = clean(t, 24).toLower();
            if (!tag.isEmpty() && tags.size() < 8 && !tags.contains(tag)) {
                tags << tag;
            }
        }
        QVariantMap item{
            { "id", id },
            { "type", type },
            { "name", clean(in.value("name", id), 60) },
            { "author", clean(in.value("author"), 40) },
            { "description", in.value("description").toString().trimmed().left(500) },
            { "preview", resolve(in.value("preview").toString()) },
            { "file", file },
            { "tags", tags },
            { "downloads", in.contains("downloads") ? in.value("downloads").toInt() : -1 },
        };
        if (type == "background") {
            const QString ext = QFileInfo(QUrl(file).path()).suffix().toLower();
            item["video"] = kVideoExt.contains(ext);
        }
        // optional colors for the mini preview; otherwise read from the theme file later
        const QVariantMap colors = in.value("colors").toMap();
        if (type == "theme" && !colors.isEmpty()) {
            QVariantMap c;
            for (const char* key : { "bg", "surface", "text", "accent" }) {
                const QString v = colors.value(key).toString();
                if (QRegularExpression("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$").match(v).hasMatch()) {
                    c[key] = v;
                }
            }
            if (c.size() == 4) {
                item["colors"] = c;
            }
        }
        list.append(item);
    }
    if (list.isEmpty()) {
        *error = tr("The gallery list is empty.");
        return false;
    }
    m_items = list;
    emit itemsChanged();
    return true;
}

void GalleryBridge::fillThemeColors()
{
    const int gen = m_generation;
    for (const auto& v : std::as_const(m_items)) {
        const QVariantMap item = v.toMap();
        if (item.value("type") != "theme" || item.contains("colors")) {
            continue;
        }
        const QString id = item.value("id").toString();
        fetch(QUrl(item.value("file").toString()), kMaxTheme, [this, gen, id](const QByteArray& data, const QString& error) {
            const int i = indexOf(id);
            if (gen != m_generation || i < 0 || !error.isEmpty()) {
                return;
            }
            QJsonObject theme;
            if (!validateTheme(data, &theme).isEmpty()) {
                return;
            }
            const QJsonObject baseColors = baseTheme().value("colors").toObject();
            const QJsonObject colors = theme.value("colors").toObject();
            QVariantMap c;
            for (const char* key : { "bg", "surface", "text", "accent" }) {
                c[key] = colors.value(key).toString(baseColors.value(key).toString());
            }
            QVariantMap item = m_items[i].toMap();
            item["colors"] = c;
            m_items[i] = item;
            emit itemsChanged();
        });
    }
}

int GalleryBridge::indexOf(const QString& id) const
{
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].toMap().value("id") == id) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- validation

QString GalleryBridge::validateTheme(const QByteArray& data, QJsonObject* out)
{
    if (data.size() > kMaxTheme) {
        return tr("The theme file is too big.");
    }
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        return tr("The theme file is not valid JSON.");
    }
    const QJsonObject in = doc.object();
    const QJsonObject& base = baseTheme();
    // background keys the base doesn't list but themes may use
    const QStringList bgExtras = { "wallpaper", "video", "slideshow", "interval", "color" };
    static const QRegularExpression colorRe("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$");

    QJsonObject result;
    for (auto it = in.begin(); it != in.end(); ++it) {
        const QString key = it.key();
        const QJsonValue v = it.value();
        if (key == "name" || key == "author" || key == "description") {
            if (!v.isString() || v.toString().size() > 500) {
                return tr("“%1” must be a short text.").arg(key);
            }
            result[key] = v.toString().left(key == "description" ? 500 : 60);
            continue;
        }
        if (key == "dark") {
            if (!v.isBool()) {
                return tr("“dark” must be true or false.");
            }
            result[key] = v;
            continue;
        }
        if (!base.contains(key) || !base.value(key).isObject()) {
            return tr("Unknown setting “%1”.").arg(key);
        }
        if (!v.isObject()) {
            return tr("“%1” must be a group of settings.").arg(key);
        }
        const QJsonObject group = v.toObject();
        const QJsonObject baseGroup = base.value(key).toObject();
        QJsonObject cleanGroup;
        for (auto g = group.begin(); g != group.end(); ++g) {
            const QString name = key + "." + g.key();
            const QJsonValue gv = g.value();
            if (key == "colors") {
                if (!baseGroup.contains(g.key())) {
                    return tr("Unknown setting “%1”.").arg(name);
                }
                if (!gv.isString() || !colorRe.match(gv.toString()).hasMatch()) {
                    return tr("“%1” must be a color like #1E2128.").arg(name);
                }
            } else if (baseGroup.contains(g.key())) {
                const QJsonValue bv = baseGroup.value(g.key());
                const bool sameType = bv.type() == gv.type();
                if (!sameType || (gv.isString() && gv.toString().size() > 100) || (gv.isDouble() && !std::isfinite(gv.toDouble()))) {
                    return tr("“%1” has a wrong value.").arg(name);
                }
                if (key == "background" && g.key() == "image" && !gv.toString().isEmpty() && !plainFileName(gv.toString())) {
                    return tr("“%1” must be a plain file name.").arg(name);
                }
            } else if (key == "background" && bgExtras.contains(g.key())) {
                if (g.key() == "interval") {
                    if (!gv.isDouble()) {
                        return tr("“%1” has a wrong value.").arg(name);
                    }
                } else if (g.key() == "slideshow") {
                    if (!gv.isArray() || gv.toArray().size() > 50) {
                        return tr("“%1” has a wrong value.").arg(name);
                    }
                    for (const auto& e : gv.toArray()) {
                        if (!e.isString() || !plainFileName(e.toString())) {
                            return tr("“%1” has a wrong value.").arg(name);
                        }
                    }
                } else if (g.key() == "color") {
                    if (!gv.isString() || (!gv.toString().isEmpty() && !colorRe.match(gv.toString()).hasMatch())) {
                        return tr("“%1” must be a color like #1E2128.").arg(name);
                    }
                } else if (!gv.isString() || (!gv.toString().isEmpty() && !plainFileName(gv.toString()))) {
                    return tr("“%1” must be a plain file name.").arg(name);
                }
            } else {
                return tr("Unknown setting “%1”.").arg(name);
            }
            cleanGroup[g.key()] = gv;
        }
        result[key] = cleanGroup;
    }
    if (out) {
        *out = result;
    }
    return {};
}

// ---------------------------------------------------------------- install

void GalleryBridge::setBusy(const QString& id)
{
    m_busyId = id;
    emit stateChanged();
}

void GalleryBridge::install(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0 || !m_busyId.isEmpty()) {
        return;
    }
    const QVariantMap item = m_items[i].toMap();
    const QUrl url(item.value("file").toString());
    setBusy(id);
    if (item.value("type") == "theme") {
        fetch(url, kMaxTheme, [this, id](const QByteArray& data, const QString& error) { finishTheme(id, data, error); });
    } else {
        fetch(url, kMaxBackground,
              [this, id, url](const QByteArray& data, const QString& error) { finishBackground(id, url.path(), data, error); });
    }
}

void GalleryBridge::finishTheme(const QString& id, const QByteArray& data, const QString& error)
{
    setBusy({});
    const int i = indexOf(id);
    if (!error.isEmpty() || i < 0) {
        emit installFinished(id, false, "theme", {}, tr("Couldn't download the theme: %1").arg(error));
        return;
    }
    QJsonObject theme;
    if (const QString problem = validateTheme(data, &theme); !problem.isEmpty()) {
        emit installFinished(id, false, "theme", {}, tr("This theme can't be installed. %1").arg(problem));
        return;
    }
    const QVariantMap item = m_items[i].toMap();
    if (!theme.contains("name")) {
        theme["name"] = item.value("name").toString();
    }
    if (!theme.contains("author") && !item.value("author").toString().isEmpty()) {
        theme["author"] = item.value("author").toString();
    }

    // Reinstalling overwrites our own earlier copy; never a theme the player made with the same name.
    QString base = m_installed.value(id).toString();
    base = base.startsWith("user:") ? base.mid(5) : QString();
    if (base.isEmpty()) {
        base = id;
        for (int n = 2; QFile::exists(FS::PathCombine(m_themesDir, base + ".json")); n++) {
            base = QString("%1-%2").arg(id).arg(n);
        }
    }
    QDir().mkpath(m_themesDir);
    QSaveFile f(FS::PathCombine(m_themesDir, base + ".json"));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(theme).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
        emit installFinished(id, false, "theme", {}, tr("Couldn't save the theme."));
        return;
    }
    const QString themeId = "user:" + base;
    markInstalled(id, themeId);
    emit installFinished(id, true, "theme", themeId, {});
}

void GalleryBridge::finishBackground(const QString& id, const QString& path, const QByteArray& data, const QString& error)
{
    setBusy({});
    if (!error.isEmpty()) {
        emit installFinished(id, false, "background", {}, tr("Couldn't download the background: %1").arg(error));
        return;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    if (kImageExt.contains(ext)) {
        // make sure it really is a picture we can show
        QBuffer buf;
        buf.setData(data);
        QImageReader reader(&buf);
        const QSize size = reader.size();
        if (!reader.canRead() || size.isEmpty() || size.width() > 10000 || size.height() > 10000) {
            emit installFinished(id, false, "background", {}, tr("This file isn't a picture the launcher can show."));
            return;
        }
    } else if (!kVideoExt.contains(ext)) {
        emit installFinished(id, false, "background", {}, tr("This kind of file can't be used as a background."));
        return;
    }
    if (!m_tmp) {
        m_tmp = std::make_unique<QTemporaryDir>();
    }
    // the file name becomes the background's name in the player's list
    const QString local = m_tmp->filePath(id + "." + ext);
    QFile f(local);
    if (!m_tmp->isValid() || !f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        emit installFinished(id, false, "background", {}, tr("Couldn't save the background."));
        return;
    }
    f.close();
    emit installFinished(id, true, "background", QUrl::fromLocalFile(local).toString(), {});
}

// ---------------------------------------------------------------- installed state

void GalleryBridge::loadInstalled()
{
    QFile f(FS::PathCombine(APPLICATION->dataRoot(), "ui-gallery.json"));
    if (f.open(QIODevice::ReadOnly)) {
        m_installed = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    }
    recheckInstalled();
}

void GalleryBridge::saveInstalled() const
{
    QSaveFile f(FS::PathCombine(APPLICATION->dataRoot(), "ui-gallery.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m_installed)).toJson());
        f.commit();
    }
}

void GalleryBridge::markInstalled(const QString& id, const QString& value)
{
    if (id.isEmpty() || value.isEmpty()) {
        return;
    }
    m_installed[id] = value;
    saveInstalled();
    recheckInstalled();
}

void GalleryBridge::recheckInstalled()
{
    QVariantMap live;
    for (auto it = m_installed.cbegin(); it != m_installed.cend(); ++it) {
        const QString v = it.value().toString();
        const QString file = v.startsWith("user:") ? v.mid(5) + ".json" : v;
        if (plainFileName(file) && QFile::exists(FS::PathCombine(m_themesDir, file))) {
            live[it.key()] = v;
        }
    }
    if (live != m_installedLive) {
        m_installedLive = live;
        emit installedChanged();
    }
}
