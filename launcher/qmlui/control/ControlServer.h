// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>

class QLocalServer;
class QLocalSocket;
class SearchModel;
class MinecraftInstance;

/**
 * Remote control of the running launcher for AI agents and scripts.
 *
 * Listens on a local socket that only this OS user can open (no network port) and announces itself in
 * ControlSpec::registryDir() so `fashionlauncher ctl` / `fashionlauncher mcp` can find it, also when the
 * launcher runs with -d. One JSON object per line each way:
 *   → {"id": 1, "cmd": "list_instances", "args": {}}
 *   ← {"id": 1, "ok": true, "result": …}   or   {"id": 1, "ok": false, "error": "…"}
 *
 * Commands are described in ControlSpec. Account tokens are never returned; deleting an instance asks the
 * player in the launcher window first.
 */
class ControlServer : public QObject {
    Q_OBJECT
   public:
    explicit ControlServer(QObject* parent = nullptr);
    ~ControlServer() override;

    /// Start listening and announce this launcher. Returns false if the socket could not be opened.
    bool start();

    /// The local socket name for a data folder (also used by the clients)
    static QString socketName(const QString& dataPath);

   private:
    using Reply = std::function<void(const QJsonValue& result, const QString& error)>;

    void onConnection();
    void onLine(QLocalSocket* socket, const QByteArray& line);
    void dispatch(const QString& cmd, const QJsonObject& args, const Reply& reply);
    void writeRegistry();
    void removeRegistry();

    // commands
    void status(const Reply& reply);
    void listInstances(const Reply& reply);
    void getInstance(MinecraftInstance* inst, const Reply& reply);
    void listVersions(const QJsonObject& args, const Reply& reply);
    void createInstance(const QJsonObject& args, const Reply& reply);
    void deleteInstance(MinecraftInstance* inst, const Reply& reply);
    void setInstanceSettings(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply);
    void listMods(MinecraftInstance* inst, const QString& kind, const Reply& reply);
    void searchMods(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply);
    void installMods(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply);
    void changeMod(MinecraftInstance* inst, const QJsonObject& args, bool remove, const Reply& reply);
    void listWorlds(MinecraftInstance* inst, const Reply& reply);
    void launch(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply);
    void getLog(MinecraftInstance* inst, const QJsonObject& args, const Reply& reply);
    void diagnose(MinecraftInstance* inst, const Reply& reply);
    void listAccounts(const Reply& reply);
    void setDefaultAccount(const QJsonObject& args, const Reply& reply);

    /// The search model of one instance + kind + platform, kept so install_mods can use search results
    SearchModel* searchModel(MinecraftInstance* inst, const QString& kind, const QString& platform, const QString& query);

    QLocalServer* m_server = nullptr;
    QString m_registryFile;
    QHash<QString, QPointer<SearchModel>> m_searches;
    QStringList m_searchOrder;  // oldest first
};
