// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <memory>

#include "minecraft/auth/MinecraftAccount.h"
#include "net/NetJob.h"

class SkinList;

/**
 * Skins: 3D preview, local skin library, upload/cape for Microsoft accounts.
 * Exposed to QML as "Skins". The library is Prism's skin folder (setting SkinsDir, default
 * `<data>/skins`, with index.json for model/cape), so the classic dialog sees the same skins.
 */
class SkinsBridge : public QObject {
    Q_OBJECT
    /// [{ id, name, microsoft (bool), owns (bool: can upload) }]
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QString accountId READ accountId WRITE setAccountId NOTIFY accountChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountChanged)
    /// The account can change its skin on Mojang's servers (signed-in Microsoft account that owns the game)
    Q_PROPERTY(bool canUpload READ canUpload NOTIFY accountChanged)
    Q_PROPERTY(bool microsoft READ microsoft NOTIFY accountChanged)
    /// [{ key, name, path (local file), slim, capeId, inUse }]
    Q_PROPERTY(QVariantList skins READ skins NOTIFY skinsChanged)
    /// [{ id, name, path }] capes the account owns (paths may be empty until downloaded)
    Q_PROPERTY(QVariantList capes READ capes NOTIFY capesChanged)
    Q_PROPERTY(QString currentCape READ currentCape NOTIFY accountChanged)
    Q_PROPERTY(QString skinsFolder READ skinsFolder NOTIFY accountChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)

   public:
    explicit SkinsBridge(QObject* parent = nullptr);
    ~SkinsBridge() override;

    QVariantList accounts() const;
    QString accountId() const;
    void setAccountId(const QString& id);
    QString accountName() const;
    bool canUpload() const;
    bool microsoft() const;
    QVariantList skins() const { return m_skins; }
    QVariantList capes() const { return m_capes; }
    QString currentCape() const;
    QString skinsFolder() const;
    bool busy() const { return !m_busyText.isEmpty(); }
    QString busyText() const { return m_busyText; }

    /// Build the library (the Skins page calls this when it opens); later calls just refresh it
    Q_INVOKABLE void load();
    /// Copy skin PNGs into the library. Returns how many were added; problems go to `message`.
    Q_INVOKABLE int addFiles(const QVariant& urls);
    /// Download the skin a player currently wears (Mojang API) into the library
    Q_INVOKABLE void addFromPlayer(const QString& playerName);
    Q_INVOKABLE void remove(const QString& key);
    Q_INVOKABLE bool rename(const QString& key, const QString& newName);
    Q_INVOKABLE void setSlim(const QString& key, bool slim);
    Q_INVOKABLE void setCape(const QString& key, const QString& capeId);
    /// Upload the skin (and its cape choice) to the Microsoft account
    Q_INVOKABLE void use(const QString& key);
    /// Go back to the default skin on the Microsoft account
    Q_INVOKABLE void resetToDefault();
    Q_INVOKABLE void openFolder() const;
    /// Guess slim/classic from the texture (slim arms leave a transparent column)
    Q_INVOKABLE bool looksSlim(const QString& path) const;

   signals:
    void accountsChanged();
    void accountChanged();
    void skinsChanged();
    void capesChanged();
    void busyChanged();
    /// Something to tell the user (a toast)
    void message(const QString& text, bool isError);
    /// A skin was added; QML selects it
    void added(const QString& key);

   private:
    MinecraftAccountPtr account() const;
    void rebuildList();
    void rebuildSkins();
    void rebuildCapes();
    void setBusy(const QString& text);
    void runJob(NetJob::Ptr job, const QString& busyText, std::function<void(bool, QString)> done);

    QString m_accountId;
    MinecraftAccountPtr m_fallback;  // used when there are no accounts at all
    std::unique_ptr<SkinList> m_list;
    QVariantList m_skins;
    QVariantList m_capes;
    QString m_busyText;
    NetJob::Ptr m_job;
    NetJob::Ptr m_capeJob;
};
