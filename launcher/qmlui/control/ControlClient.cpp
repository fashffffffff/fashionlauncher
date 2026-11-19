// SPDX-License-Identifier: GPL-3.0-only
#include "ControlClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QStringList>
#include <QThread>
#include <cstdio>
#include <iostream>
#include <string>

#include "BuildConfig.h"
#include "ControlServer.h"
#include "ControlSpec.h"

namespace {

void err(const QString& text)
{
    std::cerr << text.toStdString() << std::endl;
}

/// Connection to the running launcher's ControlServer
class Link {
   public:
    explicit Link(QString dataDir) : m_dataDir(std::move(dataDir)) {}

    /// Sends one command and waits for its answer. Starts the launcher if it isn't running.
    QJsonObject call(const QString& cmd, const QJsonObject& args, int timeoutMs)
    {
        if (!ensure()) {
            return QJsonObject{ { "ok", false }, { "error", m_error } };
        }
        const QJsonObject req{ { "id", ++m_id }, { "cmd", cmd }, { "args", args } };
        m_socket.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
        m_socket.flush();
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            while (m_socket.canReadLine()) {
                const auto doc = QJsonDocument::fromJson(m_socket.readLine());
                if (doc.object().value("id").toInt() == m_id) {
                    return doc.object();
                }
            }
            if (m_socket.state() != QLocalSocket::ConnectedState) {
                return QJsonObject{ { "ok", false }, { "error", "The launcher closed the connection" } };
            }
            m_socket.waitForReadyRead(250);
        }
        return QJsonObject{ { "ok", false }, { "error", "The launcher did not answer in time" } };
    }

   private:
    bool tryConnect(const QString& name)
    {
        m_socket.abort();
        m_socket.connectToServer(name);
        return m_socket.waitForConnected(1500);
    }

    /// Socket names of running launchers from the registry (the default data folder first)
    QStringList candidates()
    {
        QStringList preferred, others;
        QDir dir(ControlSpec::registryDir());
        for (const auto& f : dir.entryList({ "*.json" }, QDir::Files, QDir::Time)) {
            QFile file(dir.filePath(f));
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const auto o = QJsonDocument::fromJson(file.readAll()).object();
            const QString name = o.value("socket").toString();
            if (name.isEmpty()) {
                continue;
            }
            (o.value("custom_data").toBool() ? others : preferred).append(name);
        }
        return preferred + others;
    }

    bool ensure()
    {
        if (m_socket.state() == QLocalSocket::ConnectedState) {
            return true;
        }
        auto connectAny = [this] {
            if (!m_dataDir.isEmpty()) {
                return tryConnect(ControlServer::socketName(m_dataDir));
            }
            for (const auto& name : candidates()) {
                if (tryConnect(name)) {
                    return true;
                }
            }
            return false;
        };
        if (connectAny()) {
            return true;
        }
        // Not running: start it (with the same data folder) and wait for it to come up
        QStringList args;
        if (!m_dataDir.isEmpty()) {
            args << "-d" << m_dataDir;
        }
        err(QString("Starting %1…").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) {
            m_error = "Could not start the launcher";
            return false;
        }
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 90000) {
            QThread::msleep(500);
            if (connectAny()) {
                return true;
            }
        }
        m_error = "The launcher started but its remote control did not come up";
        return false;
    }

    QString m_dataDir;
    QLocalSocket m_socket;
    QString m_error;
    int m_id = 0;
};

/// Long jobs (creating instances, installing mods) get more time
int timeoutFor(const QString& cmd)
{
    if (cmd == "create_instance" || cmd == "install_mods") {
        return 30 * 60 * 1000;
    }
    if (cmd == "delete_instance") {
        return 10 * 60 * 1000;  // waits for the player's answer
    }
    return 2 * 60 * 1000;
}

QJsonObject toolSpec(const QString& name)
{
    for (const auto& v : ControlSpec::tools()) {
        if (v.toObject().value("name").toString() == name) {
            return v.toObject();
        }
    }
    return {};
}

/// Pulls -d / --dir out of the arguments
QString takeDataDir(QStringList& args)
{
    for (int i = 0; i < args.size(); ++i) {
        if ((args[i] == "-d" || args[i] == "--dir") && i + 1 < args.size()) {
            const QString dir = args[i + 1];
            args.remove(i, 2);
            return dir;
        }
    }
    return {};
}

// ---- ctl -----------------------------------------------------------------------------------------

void printHelp(const QString& command)
{
    if (!command.isEmpty()) {
        const auto t = toolSpec(command);
        if (t.isEmpty()) {
            err(QString("Unknown command \"%1\"").arg(command));
            return;
        }
        std::cout << command.toStdString() << " — " << t.value("description").toString().toStdString() << "\n\n";
        const auto schema = t.value("inputSchema").toObject();
        const auto required = schema.value("required").toArray();
        const auto props = schema.value("properties").toObject();
        for (auto it = props.begin(); it != props.end(); ++it) {
            const auto p = it.value().toObject();
            QString line = QString("  --%1 <%2>").arg(it.key(), p.value("type").toString());
            if (required.contains(it.key())) {
                line += " (required)";
            }
            std::cout << line.toStdString() << "\n      " << p.value("description").toString().toStdString();
            if (p.contains("enum")) {
                QStringList choices;
                for (const auto& c : p.value("enum").toArray()) {
                    choices << c.toString();
                }
                std::cout << " [" << choices.join(", ").toStdString() << "]";
            }
            std::cout << "\n";
        }
        return;
    }
    std::cout << "Remote control of " << BuildConfig.LAUNCHER_DISPLAYNAME.toStdString() << " (output is JSON).\n\n"
              << "  fashionlauncher ctl [-d DIR] <command> [instance] [--arg value …] [--json '{…}']\n"
              << "  fashionlauncher ctl help <command>\n"
              << "  fashionlauncher mcp [-d DIR]     MCP server for AI agents (stdin/stdout)\n\nCommands:\n";
    for (const auto& v : ControlSpec::tools()) {
        const auto o = v.toObject();
        std::cout << "  " << o.value("name").toString().leftJustified(24).toStdString()
                  << o.value("description").toString().section(". ", 0, 0).toStdString() << "\n";
    }
}

QJsonValue coerce(const QString& raw, const QString& type)
{
    if (type == "integer") {
        bool ok = false;
        const int n = raw.toInt(&ok);
        return ok ? QJsonValue(n) : QJsonValue(raw);
    }
    if (type == "boolean") {
        return QJsonValue(raw == "true" || raw == "1" || raw == "yes" || raw == "on");
    }
    if (type == "array") {
        return QJsonArray::fromStringList(raw.split(',', Qt::SkipEmptyParts));
    }
    return raw;
}

int runCtl(QStringList args, const QString& dataDir)
{
    if (args.isEmpty() || args.first() == "help" || args.first() == "--help" || args.first() == "-h") {
        printHelp(args.size() > 1 ? args.at(1) : QString());
        return 0;
    }
    if (args.first() == "tools") {
        std::cout << QJsonDocument(ControlSpec::tools()).toJson().toStdString();
        return 0;
    }
    const QString cmd = args.takeFirst();
    const auto spec = toolSpec(cmd);
    if (spec.isEmpty()) {
        err(QString("Unknown command \"%1\". Run: fashionlauncher ctl help").arg(cmd));
        return 2;
    }
    const auto props = spec.value("inputSchema").toObject().value("properties").toObject();
    QJsonObject callArgs;
    for (int i = 0; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == "--json" && i + 1 < args.size()) {
            const auto o = QJsonDocument::fromJson(args.at(++i).toUtf8()).object();
            for (auto it = o.begin(); it != o.end(); ++it) {
                callArgs[it.key()] = it.value();
            }
        } else if (a.startsWith("--")) {
            const QString key = a.mid(2).replace('-', '_');
            const QString type = props.value(key).toObject().value("type").toString("string");
            if (type == "boolean" && (i + 1 >= args.size() || args.at(i + 1).startsWith("--"))) {
                callArgs[key] = true;  // a bare flag
            } else if (i + 1 < args.size()) {
                callArgs[key] = coerce(args.at(++i), type);
            }
        } else if (props.contains("instance") && !callArgs.contains("instance")) {
            callArgs["instance"] = a;  // first bare word: the instance
        }
    }
    Link link(dataDir);
    const auto res = link.call(cmd, callArgs, timeoutFor(cmd));
    if (!res.value("ok").toBool()) {
        err(res.value("error").toString("Failed"));
        return 1;
    }
    const auto result = res.value("result");
    std::cout << (result.isObject() ? QJsonDocument(result.toObject()).toJson()
                  : result.isArray() ? QJsonDocument(result.toArray()).toJson()
                                     : QJsonDocument(QJsonArray{ result }).toJson())
                     .toStdString();
    return 0;
}

// ---- mcp -----------------------------------------------------------------------------------------

void send(const QJsonObject& msg)
{
    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

QJsonObject textResult(const QString& text, bool isError)
{
    return QJsonObject{ { "content", QJsonArray{ QJsonObject{ { "type", "text" }, { "text", text } } } }, { "isError", isError } };
}

int runMcp(const QString& dataDir)
{
    Link link(dataDir);
    std::string raw;
    while (std::getline(std::cin, raw)) {
        const QByteArray line = QByteArray::fromStdString(raw).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto msg = QJsonDocument::fromJson(line).object();
        const QString method = msg.value("method").toString();
        const QJsonValue id = msg.value("id");
        const bool isRequest = msg.contains("id") && !method.isEmpty();
        auto respond = [&id](const QJsonValue& result) { send(QJsonObject{ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } }); };
        auto fail = [&id](int code, const QString& text) {
            send(QJsonObject{ { "jsonrpc", "2.0" }, { "id", id }, { "error", QJsonObject{ { "code", code }, { "message", text } } } });
        };

        if (method == "initialize") {
            const QString wanted = msg.value("params").toObject().value("protocolVersion").toString();
            respond(QJsonObject{
                { "protocolVersion", wanted.isEmpty() ? QString("2025-06-18") : wanted },
                { "capabilities", QJsonObject{ { "tools", QJsonObject{ { "listChanged", false } } } } },
                { "serverInfo", QJsonObject{ { "name", "fashion-launcher" }, { "version", BuildConfig.printableVersionString() } } },
                { "instructions",
                  QString("Controls %1, a Minecraft launcher on this computer. Start with status or list_instances. To add "
                          "mods: search_mods, then install_mods with the project ids. launch returns at once; follow it with "
                          "status and get_log. delete_instance asks the player to confirm in the launcher window.")
                      .arg(BuildConfig.LAUNCHER_DISPLAYNAME) },
            });
        } else if (method == "ping") {
            respond(QJsonObject{});
        } else if (method == "tools/list") {
            respond(QJsonObject{ { "tools", ControlSpec::tools() } });
        } else if (method == "tools/call") {
            const auto params = msg.value("params").toObject();
            const QString name = params.value("name").toString();
            if (!ControlSpec::has(name)) {
                fail(-32602, QString("Unknown tool \"%1\"").arg(name));
                continue;
            }
            const auto res = link.call(name, params.value("arguments").toObject(), timeoutFor(name));
            if (!res.value("ok").toBool()) {
                respond(textResult(res.value("error").toString("Failed"), true));
                continue;
            }
            const auto result = res.value("result");
            const QString text = result.isObject() ? QString::fromUtf8(QJsonDocument(result.toObject()).toJson(QJsonDocument::Indented))
                                 : result.isArray() ? QString::fromUtf8(QJsonDocument(result.toArray()).toJson(QJsonDocument::Indented))
                                                    : result.toVariant().toString();
            auto out = textResult(text, false);
            if (result.isObject()) {
                out["structuredContent"] = result.toObject();
            }
            respond(out);
        } else if (isRequest) {
            fail(-32601, QString("Method not found: %1").arg(method));
        }
        // notifications (initialized, cancelled, …) need no answer
    }
    return 0;
}

}  // namespace

namespace ControlClient {

bool wanted(int argc, char* argv[])
{
    if (argc < 2) {
        return false;
    }
    const QByteArray first(argv[1]);
    return first == "ctl" || first == "mcp";
}

int run(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(BuildConfig.LAUNCHER_NAME);
    QStringList args = app.arguments().mid(1);
    const QString mode = args.takeFirst();
    const QString dataDir = takeDataDir(args);
    return mode == "mcp" ? runMcp(dataDir) : runCtl(args, dataDir);
}

}  // namespace ControlClient
