// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QPointer>

#include <functional>
#include <memory>

namespace nbt {
class tag_compound;
}

/**
 * The servers of one instance (its servers.dat), with live status from a Server List Ping.
 * QML: `import FFPrism.Servers` then `ServersModel { instanceId: ... }`.
 * Edits are written back to servers.dat right away, in the same format Minecraft uses.
 */
class ServerListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString instanceId READ instanceId WRITE setInstanceId NOTIFY instanceIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool pinging READ pinging NOTIFY pingingChanged)
    /// Ping every server as soon as the list loads
    Q_PROPERTY(bool autoPing MEMBER m_autoPing NOTIFY autoPingChanged)

   public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AddressRole,
        IconRole,       // data: URL of the server's favicon, or empty
        StatusRole,     // "unknown" | "pinging" | "online" | "offline"
        PlayersRole,    // -1 when unknown
        MaxPlayersRole,
        MotdRole,       // rich text (HTML subset), colors from the server
        MotdPlainRole,
        LatencyRole,    // ms, -1 when unknown
        VersionRole,
        ErrorRole,
    };

    explicit ServerListModel(QObject* parent = nullptr);
    ~ServerListModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString instanceId() const { return m_instanceId; }
    void setInstanceId(const QString& id);
    int count() const { return m_servers.size(); }
    bool pinging() const { return m_pending > 0; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void pingAll();
    Q_INVOKABLE void ping(int row);
    Q_INVOKABLE bool add(const QString& name, const QString& address);
    Q_INVOKABLE bool edit(int row, const QString& name, const QString& address);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void move(int from, int to);
    /// Start the instance and connect straight to this server
    Q_INVOKABLE bool join(int row);
    /// Empty when the address looks usable, otherwise a plain explanation
    Q_INVOKABLE QString addressProblem(const QString& address) const;

    struct Entry {
        QString name;
        QString address;
        QByteArray icon;   // PNG bytes as stored in servers.dat
        std::shared_ptr<nbt::tag_compound> raw;  // original tag, so unknown keys survive a save
        // live
        QString status = "unknown";
        int players = -1;
        int maxPlayers = -1;
        QString motd;
        QString motdPlain;
        int latency = -1;
        QString version;
        QString error;
        quint64 pingToken = 0;
    };

   signals:
    void instanceIdChanged();
    void countChanged();
    void pingingChanged();
    void autoPingChanged();
    void failed(const QString& reason);

   private:
    QString serversPath() const;
    bool save();
    void finishPing(quint64 token, const std::function<void(Entry&)>& apply);
    void touch(int row);

    QString m_instanceId;
    QList<Entry> m_servers;
    bool m_autoPing = true;
    int m_pending = 0;
    quint64 m_nextToken = 1;
};
