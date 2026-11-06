// SPDX-License-Identifier: GPL-3.0-only
#include "PromptsBridge.h"

#include <QEventLoop>

#include "Application.h"
#include "BaseInstance.h"
#include "minecraft/auth/AccountList.h"
#include "qmlui/LauncherBridge.h"
#include "tasks/Task.h"

struct PromptsBridge::Pending {
    QEventLoop loop;
    QVariant value;
};

PromptsBridge::PromptsBridge(LauncherBridge* launcher, QObject* parent) : QObject(parent), m_launcher(launcher) {}

PromptsBridge::~PromptsBridge()
{
    // The window is going away: every open question counts as dismissed.
    for (const auto& weak : std::as_const(m_pending)) {
        if (auto pending = weak.lock()) {
            pending->loop.quit();
        }
    }
    if (m_waitedTask) {
        m_waitedTask->disconnect(this);
    }
}

QVariant PromptsBridge::run(QVariantMap request)
{
    const int id = ++m_nextId;
    request["id"] = id;
    auto pending = std::make_shared<Pending>();
    m_pending.insert(id, pending);
    QPointer<PromptsBridge> self(this);
    emit requested(request);
    pending->loop.exec(QEventLoop::DialogExec);
    if (self) {
        m_pending.remove(id);
    }
    return pending->value;
}

void PromptsBridge::answer(int id, const QVariant& value)
{
    if (auto pending = m_pending.value(id).lock()) {
        pending->value = value;
        pending->loop.quit();
    }
}

int PromptsBridge::ask(const QString& title, const QString& text, const QStringList& buttons, const QString& tone)
{
    const QVariant v = run({ { "kind", "ask" }, { "title", title }, { "text", text }, { "buttons", buttons }, { "tone", tone } });
    return v.isValid() ? v.toInt() : -1;
}

MinecraftAccountPtr PromptsBridge::chooseAccount(const QString& title, bool* useAsDefault)
{
    const QVariantMap v = run({ { "kind", "account" }, { "title", title } }).toMap();
    if (useAsDefault) {
        *useAsDefault = v.value("useAsDefault").toBool();
    }
    const QString id = v.value("accountId").toString();
    auto* accounts = APPLICATION->accounts();
    for (int i = 0; i < accounts->count(); i++) {
        if (accounts->at(i)->internalId() == id) {
            return accounts->at(i);
        }
    }
    return nullptr;
}

QString PromptsBridge::askPlayerName(const QString& title, const QString& message, const QString& name, bool* ok)
{
    const QVariant v = run({ { "kind", "name" }, { "title", title }, { "text", message }, { "name", name } });
    if (ok) {
        *ok = v.isValid() && !v.toString().trimmed().isEmpty();
    }
    return v.toString().trimmed();
}

bool PromptsBridge::waitFor(Task* task, const QString& title)
{
    if (!task || task->isFinished()) {
        return true;
    }
    if (!task->isRunning() && task->getState() != Task::State::Inactive) {
        return true;
    }
    m_waitedTask = task;
    m_task = { { "active", true }, { "title", title }, { "status", task->getStatus() }, { "progress", -1 } };
    emit taskChanged();

    QEventLoop loop;
    bool cancelled = false;
    connect(task, &Task::status, this, [this](const QString& status) {
        m_task["status"] = status;
        emit taskChanged();
    });
    connect(task, &Task::progress, this, [this](qint64 current, qint64 total) {
        m_task["progress"] = total > 0 ? double(current) / double(total) : -1.0;
        emit taskChanged();
    });
    connect(task, &Task::finished, &loop, &QEventLoop::quit);
    connect(task, &Task::aborted, &loop, [&cancelled] { cancelled = true; });
    connect(task, &QObject::destroyed, &loop, &QEventLoop::quit);
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    QPointer<PromptsBridge> self(this);
    loop.exec(QEventLoop::DialogExec);

    if (self) {
        if (m_waitedTask) {
            m_waitedTask->disconnect(this);
        }
        m_waitedTask.clear();
        m_task.clear();
        emit taskChanged();
    }
    return !cancelled;
}

void PromptsBridge::cancelTask()
{
    if (m_waitedTask && m_waitedTask->canAbort()) {
        m_waitedTask->abort();
    }
}

void PromptsBridge::trackStep(BaseInstance* instance, Task* task)
{
    if (m_launcher && instance) {
        m_launcher->trackStep(instance->id(), task);
    }
}

void PromptsBridge::showInstance(BaseInstance* instance, const QString& page)
{
    if (m_launcher && instance) {
        m_launcher->editInstance(instance->id(), page);
    }
}

void PromptsBridge::showSettings(const QString& page)
{
    if (m_launcher) {
        m_launcher->openSettings(page);
    }
}
