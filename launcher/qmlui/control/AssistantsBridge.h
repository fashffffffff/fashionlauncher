// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QVariantList>

/**
 * Settings → AI assistants: connects the launcher's MCP server (`fashionlauncher mcp`) to AI apps on this
 * computer — Claude Code (through its `claude mcp` command), Claude Desktop and Codex (their config files,
 * backed up before the first change) — and offers the config snippet and terminal command for anything else.
 */
class AssistantsBridge : public QObject {
    Q_OBJECT
    /// [{ id, name, detected, connected, how }]
    Q_PROPERTY(QVariantList clients READ clients NOTIFY changed)
    Q_PROPERTY(QString mcpJson READ mcpJson CONSTANT)
    Q_PROPERTY(QString ctlCommand READ ctlCommand CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

   public:
    explicit AssistantsBridge(QObject* parent = nullptr);

    QVariantList clients() const;
    QString mcpJson() const;
    QString ctlCommand() const;
    bool busy() const { return m_busy; }

    Q_INVOKABLE void connectClient(const QString& id);
    Q_INVOKABLE void disconnectClient(const QString& id);
    Q_INVOKABLE void copy(const QString& text) const;
    Q_INVOKABLE void refresh() { emit changed(); }

   signals:
    void changed();
    void message(const QString& text, bool isError);

   private:
    QStringList mcpArgs() const;
    QString claudeBinary() const;
    QString claudeDesktopConfig() const;
    QString codexConfig() const;
    bool claudeCodeConnected() const;
    bool desktopConnected() const;
    bool codexConnected() const;
    bool editDesktop(bool add);
    bool editCodex(bool add);
    void runClaude(const QStringList& args, const QString& done);

    bool m_busy = false;
};
