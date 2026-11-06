// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>
#include <functional>
#include <memory>

class QNetworkReply;

/**
 * Community gallery of themes and backgrounds: a static JSON index (hosted anywhere, e.g. a public GitHub repo),
 * preview, one-click install. Also owns the UI sound settings (read by the QML singleton Sounds).
 * Exposed to QML as "Gallery".
 *
 * Index: { "items": [ { id, type: "theme"|"background", name, author, description, preview, file, tags, downloads?,
 * colors? } ] }. preview/file may be relative to the index URL. When the index can't be fetched, the sample gallery
 * shipped with the launcher (gallery/index.json) is used instead.
 */
class GalleryBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    /// true when the items come from the bundled sample instead of indexUrl
    Q_PROPERTY(bool usingSample READ usingSample NOTIFY stateChanged)
    Q_PROPERTY(QString busyId READ busyId NOTIFY stateChanged)
    /// gallery id -> installed theme id ("user:...") or background file name, only for things still on disk
    Q_PROPERTY(QVariantMap installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(QString indexUrl READ indexUrl WRITE setIndexUrl NOTIFY indexUrlChanged)
    Q_PROPERTY(QString defaultIndexUrl READ defaultIndexUrl CONSTANT)
    Q_PROPERTY(QString submitUrl READ submitUrl CONSTANT)

    Q_PROPERTY(bool soundsOn READ soundsOn WRITE setSoundsOn NOTIFY soundsChanged)
    Q_PROPERTY(qreal soundVolume READ soundVolume WRITE setSoundVolume NOTIFY soundsChanged)

   public:
    explicit GalleryBridge(QObject* parent = nullptr);

    QVariantList items() const { return m_items; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool usingSample() const { return m_usingSample; }
    QString busyId() const { return m_busyId; }
    QVariantMap installed() const { return m_installedLive; }
    QString indexUrl() const;
    void setIndexUrl(const QString& url);
    QString defaultIndexUrl() const;
    QString submitUrl() const;

    bool soundsOn() const;
    void setSoundsOn(bool on);
    qreal soundVolume() const;
    void setSoundVolume(qreal v);

    /// Fetch the index again (falls back to the bundled sample)
    Q_INVOKABLE void refresh();
    /// Download and install one item. Themes are validated and written to the user themes folder;
    /// backgrounds are downloaded and handed back (installFinished with a file URL) for Backgrounds.importFile.
    Q_INVOKABLE void install(const QString& id);
    /// Record a background imported by QML, so the card shows "Installed"
    Q_INVOKABLE void markInstalled(const QString& id, const QString& value);
    /// Re-check which installed items still exist (themes or backgrounds may have been deleted)
    Q_INVOKABLE void recheckInstalled();

    /// Exposed for tests: returns an error message, or empty and fills out with the cleaned theme
    static QString validateTheme(const QByteArray& data, QJsonObject* out);

   signals:
    void itemsChanged();
    void stateChanged();
    void installedChanged();
    void indexUrlChanged();
    void soundsChanged();
    /// kind: "theme" (value = theme id) | "background" (value = local file URL to import)
    void installFinished(const QString& id, bool ok, const QString& kind, const QString& value, const QString& message);

   private:
    using Done = std::function<void(const QByteArray& data, const QString& error)>;
    void fetch(const QUrl& url, qint64 maxBytes, Done done);
    bool loadIndex(const QByteArray& data, const QUrl& base, QString* error);
    bool loadSample();
    QUrl sampleIndexUrl() const;
    void fillThemeColors();
    int indexOf(const QString& id) const;
    void setBusy(const QString& id);
    void finishTheme(const QString& id, const QByteArray& data, const QString& error);
    void finishBackground(const QString& id, const QString& fileUrl, const QByteArray& data, const QString& error);
    void loadInstalled();
    void saveInstalled() const;

    QVariantList m_items;
    bool m_loading = false;
    QString m_error;
    bool m_usingSample = false;
    QString m_busyId;
    QVariantMap m_installed;      // persisted
    QVariantMap m_installedLive;  // filtered to files that exist
    QString m_themesDir;
    std::unique_ptr<QTemporaryDir> m_tmp;
    int m_generation = 0;  // ignores answers for an older refresh()
    // FFPRISM_GALLERY_AUTOINSTALL=id,id: install these after the first load (manual testing)
    QStringList m_autoQueue;
    bool m_autoStarted = false;
    void autoInstallNext();
};
