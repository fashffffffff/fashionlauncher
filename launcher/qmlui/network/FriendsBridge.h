// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariant>

/**
 * Owns the `fashion-net` sidecar (Rust: DHT rendezvous, STUN, hole punching, QUIC tunnels) for the
 * whole process, so presence and game tunnels keep running while the QML window is unloaded.
 * Protocol: JSON lines; commands on stdin ({"cmd": ...}), events on stdout ({"ev": "state"|"notice"|...}).
 */
class FriendsService : public QObject {
    Q_OBJECT
   public:
    static FriendsService* instance();
    ~FriendsService() override;

    void send(const QVariantMap& cmd);
    bool running() const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    QString error() const { return m_error; }
    QVariantMap state() const { return m_state; }
    QVariantMap joinInstances;  // invite id -> instance chosen to join with

   signals:
    void stateChanged();
    void notice(const QVariantMap& notice);
    void runningChanged();

   private:
    explicit FriendsService(QObject* parent);
    void start();
    void readOutput();
    void runAutomation();
    bool m_autoDone = false;

    QProcess* m_proc = nullptr;
    QByteArray m_buffer;
    QVariantMap m_state;
    QString m_error;
    int m_restarts = 0;
};

/**
 * Serverless friends and play-together. Exposed to QML as "Friends".
 * Lists are plain QVariantLists of maps (small; replaced on every sidecar state event).
 */
class FriendsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY runningChanged)
    Q_PROPERTY(QString error READ error NOTIFY runningChanged)
    Q_PROPERTY(QVariantMap me READ me NOTIFY changed)
    Q_PROPERTY(QVariantMap net READ net NOTIFY changed)
    Q_PROPERTY(QVariantList friends READ friends NOTIFY changed)
    Q_PROPERTY(QVariantList requests READ requests NOTIFY changed)
    Q_PROPERTY(QVariantList invites READ invites NOTIFY changed)
    Q_PROPERTY(QVariantMap hosting READ hosting NOTIFY changed)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY changed)
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)

   public:
    explicit FriendsBridge(QObject* parent = nullptr);

    bool available() const;
    QString error() const;
    QVariantMap me() const;
    QVariantMap net() const;
    QVariantList friends() const;
    QVariantList requests() const;
    QVariantList invites() const;
    QVariantMap hosting() const;
    int onlineCount() const;
    QString displayName() const;

    Q_INVOKABLE void addFriend(const QString& codeOrLink);
    Q_INVOKABLE void removeFriend(const QString& key);
    Q_INVOKABLE void acceptRequest(const QString& key);
    Q_INVOKABLE void declineRequest(const QString& key);
    Q_INVOKABLE void setDisplayName(const QString& name);
    /// Host: offer an instance to friends. `invite` = {instanceId, instance, mc, manifest}
    Q_INVOKABLE void invite(const QStringList& keys, const QVariantMap& invite);
    Q_INVOKABLE void cancelInvite(const QString& id = QString());
    /// Joiner: accept starts the local tunnel (world appears in the LAN list).
    Q_INVOKABLE void replyInvite(const QString& from, const QString& id, bool accept);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void copyText(const QString& text) const;
    /// Launch an instance straight into the tunnelled world (127.0.0.1:port).
    Q_INVOKABLE bool connectInstance(const QString& instanceId, int port);
    /// Remember which instance the player joins an invite with (survives page reloads).
    Q_INVOKABLE void setJoinInstance(const QString& inviteId, const QString& instanceId);
    Q_INVOKABLE QString joinInstance(const QString& inviteId) const;
    /// Test hook: FFPRISM_FRIENDS_DIALOG=play|join opens that dialog on the Friends page.
    Q_INVOKABLE QString testDialog() const { return qEnvironmentVariable("FFPRISM_FRIENDS_DIALOG"); }
    /// True if `text` looks like a friend code or invite link.
    Q_INVOKABLE bool looksLikeCode(const QString& text) const;

   signals:
    void changed();
    void runningChanged();
    void notice(const QVariantMap& notice);
};
