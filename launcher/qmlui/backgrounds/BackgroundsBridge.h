// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

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

   signals:
    void userMediaChanged();
    void overridesChanged();

   private:
    void loadWallpapers();
    void scanUserMedia();
    void loadOverrides();
    void saveOverrides() const;

    QString m_userDir;
    QString m_overridesPath;
    QVariantList m_wallpapers;
    QVariantList m_userMedia;
    QVariantMap m_overrides;
    QFileSystemWatcher m_watcher;
};
