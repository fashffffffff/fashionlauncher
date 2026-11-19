// SPDX-License-Identifier: GPL-3.0-only
#include "AssistantsBridge.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include "Application.h"

namespace {

const char* kServerName = "fashion-launcher";

QString home()
{
    return QDir::homePath();
}

QString readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool writeAll(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    // keep the user's original once, before our first change
    if (QFile::exists(path) && !QFile::exists(path + ".before-fashion-launcher")) {
        QFile::copy(path, path + ".before-fashion-launcher");
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(data) == data.size();
}

QString tomlString(const QString& s)
{
    QString out = s;
    out.replace('\\', "\\\\").replace('"', "\\\"");
    return '"' + out + '"';
}

}  // namespace

AssistantsBridge::AssistantsBridge(QObject* parent) : QObject(parent) {}

QStringList AssistantsBridge::mcpArgs() const
{
    QStringList args{ "mcp" };
    if (APPLICATION->dataDirOverridden()) {
        args << "-d" << QDir(APPLICATION->dataRoot()).absolutePath();
    }
    return args;
}

QString AssistantsBridge::mcpJson() const
{
    const QJsonObject server{ { "command", QCoreApplication::applicationFilePath() }, { "args", QJsonArray::fromStringList(mcpArgs()) } };
    const QJsonObject root{ { "mcpServers", QJsonObject{ { kServerName, server } } } };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed();
}

QString AssistantsBridge::ctlCommand() const
{
    QString cmd = '"' + QCoreApplication::applicationFilePath() + "\" ctl";
    if (APPLICATION->dataDirOverridden()) {
        cmd += " -d \"" + QDir(APPLICATION->dataRoot()).absolutePath() + '"';
    }
    return cmd + " help";
}

// ---- where things live ---------------------------------------------------------------------------

QString AssistantsBridge::claudeBinary() const
{
    // GUI apps don't get the shell's PATH: look where the installers put it too
    if (const QString inPath = QStandardPaths::findExecutable("claude"); !inPath.isEmpty()) {
        return inPath;
    }
    const QStringList guesses{ home() + "/.local/bin/claude", home() + "/.claude/local/claude", "/opt/homebrew/bin/claude",
                               "/usr/local/bin/claude", home() + "/.npm-global/bin/claude",
                               home() + "/AppData/Roaming/npm/claude.cmd", home() + "/.local/bin/claude.exe" };
    for (const auto& g : guesses) {
        if (QFileInfo(g).isExecutable()) {
            return g;
        }
    }
    return {};
}

QString AssistantsBridge::claudeDesktopConfig() const
{
#if defined(Q_OS_MACOS)
    return home() + "/Library/Application Support/Claude/claude_desktop_config.json";
#elif defined(Q_OS_WIN)
    return QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA")) + "/Claude/claude_desktop_config.json";
#else
    return home() + "/.config/Claude/claude_desktop_config.json";
#endif
}

QString AssistantsBridge::codexConfig() const
{
    const QString codexHome = qEnvironmentVariable("CODEX_HOME");
    return (codexHome.isEmpty() ? home() + "/.codex" : codexHome) + "/config.toml";
}

bool AssistantsBridge::claudeCodeConnected() const
{
    const auto root = QJsonDocument::fromJson(readAll(home() + "/.claude.json").toUtf8()).object();
    return root.value("mcpServers").toObject().contains(kServerName);
}

bool AssistantsBridge::desktopConnected() const
{
    const auto root = QJsonDocument::fromJson(readAll(claudeDesktopConfig()).toUtf8()).object();
    return root.value("mcpServers").toObject().contains(kServerName);
}

bool AssistantsBridge::codexConnected() const
{
    return readAll(codexConfig()).contains(QString("[mcp_servers.%1]").arg(kServerName));
}

QVariantList AssistantsBridge::clients() const
{
    const bool claudeCli = !claudeBinary().isEmpty();
    const bool desktop = QFileInfo::exists(QFileInfo(claudeDesktopConfig()).absolutePath());
    const bool codex = QFileInfo::exists(QFileInfo(codexConfig()).absolutePath()) || !QStandardPaths::findExecutable("codex").isEmpty();
    return {
        QVariantMap{ { "id", "claude-code" }, { "name", "Claude Code" }, { "detected", claudeCli }, { "connected", claudeCodeConnected() } },
        QVariantMap{ { "id", "claude-desktop" }, { "name", "Claude Desktop" }, { "detected", desktop }, { "connected", desktopConnected() } },
        QVariantMap{ { "id", "codex" }, { "name", "Codex" }, { "detected", codex }, { "connected", codexConnected() } },
    };
}

// ---- connect / disconnect ------------------------------------------------------------------------

void AssistantsBridge::runClaude(const QStringList& args, const QString& done)
{
    const QString bin = claudeBinary();
    if (bin.isEmpty()) {
        emit message(tr("Claude Code was not found on this computer."), true);
        return;
    }
    m_busy = true;
    emit changed();
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc, done](int code, QProcess::ExitStatus status) {
        m_busy = false;
        const QString out = QString::fromUtf8(proc->readAllStandardError() + proc->readAllStandardOutput()).trimmed();
        proc->deleteLater();
        emit changed();
        if (status == QProcess::NormalExit && code == 0) {
            emit message(done, false);
        } else {
            emit message(tr("Claude Code said: %1").arg(out.isEmpty() ? tr("an error") : out.section('\n', 0, 0)), true);
        }
    });
    proc->start(bin, args);
}

bool AssistantsBridge::editDesktop(bool add)
{
    const QString path = claudeDesktopConfig();
    QJsonObject root = QJsonDocument::fromJson(readAll(path).toUtf8()).object();
    QJsonObject servers = root.value("mcpServers").toObject();
    if (add) {
        servers[kServerName] = QJsonObject{ { "command", QCoreApplication::applicationFilePath() },
                                            { "args", QJsonArray::fromStringList(mcpArgs()) } };
    } else {
        servers.remove(kServerName);
    }
    root["mcpServers"] = servers;
    return writeAll(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool AssistantsBridge::editCodex(bool add)
{
    const QString path = codexConfig();
    QString text = readAll(path);
    // drop our block (header up to the next table header) first, so connecting twice doesn't duplicate it
    static const QRegularExpression block(QString(R"((?ms)^\[mcp_servers\.%1\]\n.*?(?=^\[|\z))").arg(kServerName));
    text.remove(block);
    if (add) {
        QStringList args;
        for (const auto& a : mcpArgs()) {
            args << tomlString(a);
        }
        if (!text.isEmpty() && !text.endsWith('\n')) {
            text += '\n';
        }
        text += QString("\n[mcp_servers.%1]\ncommand = %2\nargs = [%3]\n")
                    .arg(kServerName, tomlString(QCoreApplication::applicationFilePath()), args.join(", "));
    }
    return writeAll(path, text.toUtf8());
}

void AssistantsBridge::connectClient(const QString& id)
{
    if (id == "claude-code") {
        QStringList args{ "mcp", "add", "--scope", "user", kServerName, "--", QCoreApplication::applicationFilePath() };
        args << mcpArgs();
        if (claudeCodeConnected()) {
            emit message(tr("Already connected to Claude Code."), false);
            return;
        }
        runClaude(args, tr("Connected to Claude Code. Start a new Claude Code session to use it."));
        return;
    }
    const bool ok = id == "claude-desktop" ? editDesktop(true) : id == "codex" ? editCodex(true) : false;
    emit changed();
    emit message(ok ? tr("Connected. Restart %1 to use it.").arg(id == "codex" ? "Codex" : "Claude Desktop")
                    : tr("The settings file could not be changed."),
                 !ok);
}

void AssistantsBridge::disconnectClient(const QString& id)
{
    if (id == "claude-code") {
        runClaude({ "mcp", "remove", "--scope", "user", kServerName }, tr("Disconnected from Claude Code."));
        return;
    }
    const bool ok = id == "claude-desktop" ? editDesktop(false) : id == "codex" ? editCodex(false) : false;
    emit changed();
    emit message(ok ? tr("Disconnected.") : tr("The settings file could not be changed."), !ok);
}

void AssistantsBridge::copy(const QString& text) const
{
    QGuiApplication::clipboard()->setText(text);
}
