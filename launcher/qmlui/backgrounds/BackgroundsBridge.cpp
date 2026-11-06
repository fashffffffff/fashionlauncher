// SPDX-License-Identifier: GPL-3.0-only
#include "BackgroundsBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include "Application.h"
#include "FileSystem.h"
#include "settings/SettingsObject.h"

namespace {
const QString kWallpaperDir = QStringLiteral(":/qmlui/wallpapers");
const QStringList kImageExt = { "png", "jpg", "jpeg", "webp", "bmp", "gif" };
const QStringList kVideoExt = { "mp4", "webm", "mov", "m4v", "mkv" };
}  // namespace

BackgroundsBridge::BackgroundsBridge(QObject* parent) : QObject(parent)
{
    m_userDir = FS::PathCombine(APPLICATION->dataRoot(), "ui-themes");
    QDir().mkpath(m_userDir);
    m_overridesPath = FS::PathCombine(APPLICATION->dataRoot(), "ui-backgrounds.json");

    m_panoramaDir = FS::PathCombine(APPLICATION->dataRoot(), "ui-panoramas");
    m_cacheDir = FS::PathCombine(APPLICATION->dataRoot(), "cache", "qmlui");
    m_net = APPLICATION->network();

    auto s = APPLICATION->settings();
    if (!s->contains("QmlPanoramaSource")) {
        s->registerSetting("QmlPanoramaSource", "auto");
    }
    if (!s->contains("QmlPackCovers")) {
        s->registerSetting("QmlPackCovers", true);
    }
    m_panoramaSource = s->get("QmlPanoramaSource").toString();

    loadWallpapers();
    scanUserMedia();
    loadOverrides();
    scanCustomPanoramas();

    // Files dropped into the themes folder by hand show up in the gallery right away.
    m_watcher.addPath(m_userDir);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scanUserMedia(); });
}

void BackgroundsBridge::loadWallpapers()
{
    QFile f(kWallpaperDir + "/wallpapers.json");
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto list = QJsonDocument::fromJson(f.readAll()).object().value("wallpapers").toArray();
    for (const auto& v : list) {
        const auto o = v.toObject();
        const QString file = o.value("file").toString();
        if (file.isEmpty() || !QFile::exists(kWallpaperDir + "/" + file)) {
            continue;
        }
        QVariantMap w;
        w["id"] = o.value("id").toString();
        w["title"] = o.value("title").toString();
        w["url"] = "qrc" + kWallpaperDir + "/" + file;
        w["tags"] = o.value("tags").toArray().toVariantList();
        m_wallpapers.append(w);
    }
}

QString BackgroundsBridge::defaultWallpaper() const
{
    return m_wallpapers.isEmpty() ? QString() : m_wallpapers.first().toMap().value("id").toString();
}

QString BackgroundsBridge::wallpaperUrl(const QString& id) const
{
    for (const auto& v : m_wallpapers) {
        const auto w = v.toMap();
        if (w.value("id").toString() == id) {
            return w.value("url").toString();
        }
    }
    return {};
}

QString BackgroundsBridge::userFileUrl(const QString& fileName) const
{
    if (fileName.isEmpty() || fileName.contains('/') || fileName.contains('\\')) {
        return {};
    }
    const QString path = FS::PathCombine(m_userDir, fileName);
    return QFile::exists(path) ? QUrl::fromLocalFile(path).toString() : QString();
}

QString BackgroundsBridge::resolve(const QString& kind, const QString& value) const
{
    if (kind == "wallpaper") {
        return wallpaperUrl(value);
    }
    if (kind == "image" || kind == "video") {
        return userFileUrl(value);
    }
    return {};
}

QString BackgroundsBridge::kindOf(const QString& fileName) const
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (kImageExt.contains(ext)) {
        return "image";
    }
    if (kVideoExt.contains(ext)) {
        return "video";
    }
    return {};
}

void BackgroundsBridge::scanUserMedia()
{
    QVariantList media;
    const auto files = QDir(m_userDir).entryInfoList(QDir::Files, QDir::Time);
    for (const auto& fi : files) {
        const QString kind = kindOf(fi.fileName());
        if (kind.isEmpty()) {
            continue;
        }
        media.append(QVariantMap{ { "file", fi.fileName() },
                                  { "url", QUrl::fromLocalFile(fi.absoluteFilePath()).toString() },
                                  { "kind", kind } });
    }
    if (media != m_userMedia) {
        m_userMedia = media;
        emit userMediaChanged();
    }
}

QString BackgroundsBridge::importFile(const QUrl& file)
{
    const QFileInfo source(file.toLocalFile());
    const QString ext = source.suffix().toLower();
    if (!source.isFile() || kindOf(source.fileName()).isEmpty()) {
        return {};
    }
    // Keep a readable name; strip anything that would be awkward inside a theme JSON or a shell.
    QString base = source.completeBaseName().toLower();
    base.replace(QRegularExpression("[^a-z0-9_-]+"), "-");
    base = base.left(48);
    if (base.isEmpty() || base == "-") {
        base = "background";
    }
    QString name = base + "." + ext;
    const QString existing = FS::PathCombine(m_userDir, name);
    // The same file picked twice is reused instead of copied again.
    if (QFileInfo(existing).exists() && QFileInfo(existing).size() == source.size()) {
        return name;
    }
    for (int i = 2; QFile::exists(FS::PathCombine(m_userDir, name)); i++) {
        name = QString("%1-%2.%3").arg(base).arg(i).arg(ext);
    }
    if (!QFile::copy(source.absoluteFilePath(), FS::PathCombine(m_userDir, name))) {
        return {};
    }
    scanUserMedia();
    return name;
}

bool BackgroundsBridge::removeUserFile(const QString& fileName)
{
    if (kindOf(fileName).isEmpty() || fileName.contains('/') || fileName.contains('\\')) {
        return false;
    }
    const bool ok = QFile::remove(FS::PathCombine(m_userDir, fileName));
    // Instances that used the file fall back to the theme background.
    bool changed = false;
    for (auto it = m_overrides.begin(); it != m_overrides.end();) {
        const auto o = it.value().toMap();
        if (o.value("kind").toString() != "wallpaper" && o.value("value").toString() == fileName) {
            it = m_overrides.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        saveOverrides();
        emit overridesChanged();
    }
    scanUserMedia();
    return ok;
}

void BackgroundsBridge::setInstanceBackground(const QString& instanceId, const QString& kind, const QString& value)
{
    if (instanceId.isEmpty() || value.isEmpty() || !QStringList{ "wallpaper", "image", "video" }.contains(kind)) {
        return;
    }
    m_overrides[instanceId] = QVariantMap{ { "kind", kind }, { "value", value } };
    saveOverrides();
    emit overridesChanged();
}

void BackgroundsBridge::clearInstanceBackground(const QString& instanceId)
{
    if (m_overrides.remove(instanceId) > 0) {
        saveOverrides();
        emit overridesChanged();
    }
}

void BackgroundsBridge::loadOverrides()
{
    QFile f(m_overridesPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    m_overrides = QJsonDocument::fromJson(f.readAll()).object().value("instances").toObject().toVariantMap();
}

void BackgroundsBridge::saveOverrides() const
{
    QSaveFile f(m_overridesPath);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    QJsonObject root;
    root["instances"] = QJsonObject::fromVariantMap(m_overrides);
    f.write(QJsonDocument(root).toJson());
    f.commit();
}
