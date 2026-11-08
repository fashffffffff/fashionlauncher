// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>

class QQmlApplicationEngine;
class UiTheme;
class InstancesModel;
class AccountsModel;
class LauncherBridge;
class PromptsBridge;
class LaunchPrompts;
class BlockedModsBridge;

/**
 * The QML main window. Owns the engine and the objects exposed to QML.
 */
class QmlShell : public QObject {
    Q_OBJECT

   public:
    explicit QmlShell(QObject* parent = nullptr);
    ~QmlShell() override;

    bool load();
    void show(bool minimized = false);
    void raise();
    QQuickWindow* window() const { return m_window; }
    /// Answers launch questions inside this window
    LaunchPrompts* prompts() const;
    /// The blocked-mods question of pack installs; null until the window is loaded
    BlockedModsBridge* blockedMods() const;

   signals:
    void isClosing();

   private slots:
    void loadTranslator();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    QQmlApplicationEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    UiTheme* m_theme = nullptr;
    InstancesModel* m_instances = nullptr;
    AccountsModel* m_accounts = nullptr;
    LauncherBridge* m_launcher = nullptr;
    PromptsBridge* m_prompts = nullptr;
    BlockedModsBridge* m_blockedMods = nullptr;
    class QTranslator* m_translator = nullptr;
};
