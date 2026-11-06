// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <memory>

#include "LaunchPrompts.h"

class LauncherBridge;
class Task;

/**
 * The QML side of LaunchPrompts (`Prompts` in QML), so launching never opens a classic dialog.
 *
 * A question is emitted as `requested({ id, kind, title, text, buttons, tone, name })` and the
 * caller waits in a local event loop until QML calls `answer(id, value)`. `kind` is
 * "ask" (value: button index), "account" (value: { accountId, useAsDefault }) or "name" (value: the name).
 * `waitFor` shows a task in the `task` property ({ active, title, status, progress }) until it ends.
 */
class PromptsBridge : public QObject, public LaunchPrompts {
    Q_OBJECT
    Q_PROPERTY(QVariantMap task READ task NOTIFY taskChanged)

   public:
    PromptsBridge(LauncherBridge* launcher, QObject* parent = nullptr);
    ~PromptsBridge() override;

    QVariantMap task() const { return m_task; }

    Q_INVOKABLE void answer(int id, const QVariant& value);
    Q_INVOKABLE void cancelTask();

    // LaunchPrompts
    int ask(const QString& title, const QString& text, const QStringList& buttons, const QString& tone) override;
    MinecraftAccountPtr chooseAccount(const QString& title, bool* useAsDefault) override;
    QString askPlayerName(const QString& title, const QString& message, const QString& name, bool* ok) override;
    bool waitFor(Task* task, const QString& title) override;
    void trackStep(BaseInstance* instance, Task* task) override;
    void showInstance(BaseInstance* instance, const QString& page) override;
    void showSettings(const QString& page) override;

   signals:
    void requested(const QVariantMap& request);
    void taskChanged();

   private:
    struct Pending;
    /// Emit the request and wait for its answer. Returns an invalid QVariant if dismissed.
    QVariant run(QVariantMap request);

    QPointer<LauncherBridge> m_launcher;
    QHash<int, std::weak_ptr<Pending>> m_pending;
    int m_nextId = 0;
    QVariantMap m_task;
    QPointer<Task> m_waitedTask;
};
