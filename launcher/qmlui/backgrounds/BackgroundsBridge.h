// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QNetworkReply;

/**
 * Background gallery: built-in wallpapers, user images and videos, per-instance backgrounds.
 * Exposed to QML as "Backgrounds".
 *
 * The active background lives in the theme JSON ("background" section, see UiTheme); this bridge only
 * knows the media: the built-in wallpapers shipped in the resources, the user's own pictures and videos
 * (copied into the user themes folder so themes stay self-contained), and per-instance overrides kept in
 * <data>/ui-backgrounds.json.
 */
class BackgroundsBridge : public QObject {
    Q_OBJECT
    /// [{ id, title, url, tags: [..] }] — built-in wallpapers
    Q_PROPERTY(QVariantList wallpapers READ wallpapers CONSTANT)
    /// [{ file, url, kind: "image" | "video" }] — the user's own media in the user themes folder
    Q_PROPERTY(QVariantList userMedia READ userMedia NOTIFY userMediaChanged)
    /// instanceId -> { kind: "wallpaper" | "image" | "video", value: id or file name }
    Q_PROPERTY(QVariantMap overrides READ overrides NOTIFY overridesChanged)
    Q_PROPERTY(QString defaultWallpaper READ defaultWallpaper CONSTANT)

    // ---- Title-screen panorama (BackgroundsPanorama.cpp) ----
    /// "auto" (from the selected instance) or the folder name of one of the user's own panoramas.
    Q_PROPERTY(QString panoramaSource READ panoramaSource WRITE setPanoramaSource NOTIFY panoramaSourceChanged)
    /// [{ name, thumb }] — panoramas the user added (<data>/ui-panoramas/<name>/panorama_0..5.png)
    Q_PROPERTY(QVariantList customPanoramas READ customPanoramas NOTIFY customPanoramasChanged)
    /// { faces: [6 urls] or [], origin: human readable, busy: bool } for the last loadPanorama() call
    Q_PROPERTY(QVariantMap panorama READ panorama NOTIFY panoramaChanged)
    /// { instanceId, state: "" | "working" | "done" | "error", message } for "Capture from my world"
    Q_PROPERTY(QVariantMap capture READ capture NOTIFY captureChanged)

    // ---- Pack covers (BackgroundsCovers.cpp) ----
    /// instanceId -> cover image URL (modpack gallery picture), only for instances that have one cached
    Q_PROPERTY(QVariantMap covers READ covers NOTIFY coversChanged)
    Q_PROPERTY(bool coversEnabled READ coversEnabled WRITE setCoversEnabled NOTIFY coversEnabledChanged)

   public:
    explicit BackgroundsBridge(QObject* parent = nullptr);

    QVariantList wallpapers() const { return m_wallpapers; }
    QVariantList userMedia() const { return m_userMedia; }
    QVariantMap overrides() const { return m_overrides; }
    QString defaultWallpaper() const;

    /// URL of a built-in wallpaper, or "" if the id is unknown.
    Q_INVOKABLE QString wallpaperUrl(const QString& id) const;
    /// URL for a file name relative to the user themes folder, or "" if it doesn't exist.
    Q_INVOKABLE QString userFileUrl(const QString& fileName) const;
    /// Resolve { kind, value } (as stored in overrides) to a URL; "" when it can't be shown.
    Q_INVOKABLE QString resolve(const QString& kind, const QString& value) const;
    /// "image" | "video" | "" by file extension.
    Q_INVOKABLE QString kindOf(const QString& fileName) const;

    /// Copy a picked image/video into the user themes folder. Returns the new file name, or "" on failure.
    Q_INVOKABLE QString importFile(const QUrl& file);
    /// Delete one of the user's own media files (only files inside the user themes folder).
    Q_INVOKABLE bool removeUserFile(const QString& fileName);

    Q_INVOKABLE void setInstanceBackground(const QString& instanceId, const QString& kind, const QString& value);
    Q_INVOKABLE void clearInstanceBackground(const QString& instanceId);

    // Panorama
    QString panoramaSource() const { return m_panoramaSource; }
    void setPanoramaSource(const QString& source);
    QVariantList customPanoramas() const { return m_customPanoramas; }
    QVariantMap panorama() const { return m_panorama; }
    QVariantMap capture() const { return m_capture; }
    /// Find the panorama to show for this instance (or the chosen custom one) in the background.
    Q_INVOKABLE void loadPanorama(const QString& instanceId);
    /// Add a panorama from a resource pack .zip, six face images or one 360° picture. Result: panoramaImported().
    Q_INVOKABLE void importPanorama(const QList<QUrl>& files);
    Q_INVOKABLE bool removePanorama(const QString& name);
    /// Install a panorama-screenshot mod from Modrinth into the instance. Progress: capture.
    Q_INVOKABLE void installPanoramaMod(const QString& instanceId);

    // Covers
    QVariantMap covers() const { return m_covers; }
    bool coversEnabled() const;
    void setCoversEnabled(bool on);
    /// Fetch (once per run) and cache the modpack picture for this instance.
    Q_INVOKABLE void requestCover(const QString& instanceId);

   signals:
    void userMediaChanged();
    void overridesChanged();
    void panoramaSourceChanged();
    void customPanoramasChanged();
    void panoramaChanged();
    void panoramaImported(const QString& name, const QString& error);
    void captureChanged();
    void coversChanged();
    void coversEnabledChanged();

   private:
    void loadWallpapers();
    void scanUserMedia();
    void loadOverrides();
    void saveOverrides() const;

    void scanCustomPanoramas();
    void setCapture(const QString& instanceId, const QString& state, const QString& message);
    void installModVersion(const QString& instanceId, const QString& modsDir, const QVariantMap& version, const QString& title,
                           const QStringList& loaders, const QString& mcVersion);
    QNetworkReply* apiGet(const QString& url, bool curseforge = false);
    void saveCover(const QString& instanceId, const QString& imageUrl);

    QString m_userDir;
    QString m_panoramaDir;
    QString m_cacheDir;
    QString m_panoramaSource;
    QVariantList m_customPanoramas;
    QVariantMap m_panorama;
    QString m_panoramaKey;
    int m_panoramaRequest = 0;
    QVariantMap m_capture;
    QVariantMap m_covers;
    QSet<QString> m_coverRequested;
    QNetworkAccessManager* m_net = nullptr;
    QString m_overridesPath;
    QVariantList m_wallpapers;
    QVariantList m_userMedia;
    QVariantMap m_overrides;
    QFileSystemWatcher m_watcher;
};
