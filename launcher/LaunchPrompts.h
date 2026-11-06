// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

#include "minecraft/auth/MinecraftAccount.h"

class Task;
class BaseInstance;

/**
 * Questions the launch path has to ask the player.
 *
 * The classic interface answers them with widget dialogs. The QML shell implements this interface so
 * that launching from it never opens a classic window: every call is answered inside the QML window.
 * The calls block (they run a local event loop, as QDialog::exec does), so LaunchController keeps its
 * simple sequential flow either way.
 */
class LaunchPrompts {
   public:
    virtual ~LaunchPrompts() = default;

    /// A question with buttons. Returns the index of the chosen button, or -1 if dismissed.
    /// `tone` is "info", "warning" or "danger" (styles the first button).
    virtual int ask(const QString& title, const QString& text, const QStringList& buttons, const QString& tone = "info") = 0;
    /// Pick one of the accounts. Returns null if cancelled.
    virtual MinecraftAccountPtr chooseAccount(const QString& title, bool* useAsDefault) = 0;
    /// Ask for an offline player name. `ok` is false if cancelled.
    virtual QString askPlayerName(const QString& title, const QString& message, const QString& name, bool* ok) = 0;
    /// Show the task's progress until it finishes. Returns false if the player cancelled it.
    virtual bool waitFor(Task* task, const QString& title) = 0;
    /// Report a launch step's progress for this instance without blocking.
    virtual void trackStep(BaseInstance* instance, Task* task) = 0;
    /// Open an instance screen ("" overview, "console"/"log", "settings", ...).
    virtual void showInstance(BaseInstance* instance, const QString& page) = 0;
    /// Open a settings screen ("accounts" opens the accounts panel).
    virtual void showSettings(const QString& page) = 0;
};
