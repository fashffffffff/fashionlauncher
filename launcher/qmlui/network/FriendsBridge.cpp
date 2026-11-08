// SPDX-License-Identifier: GPL-3.0-only
#include "FriendsBridge.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "settings/SettingsObject.h"

namespace {
QString sidecarPath()
{
    QString name = "fashion-net";
#ifdef Q_OS_WIN
    name += ".exe";
#endif
    const QString dir = QCoreApplication::applicationDirPath();
    for (const QString& candidate : { dir + "/" + name, dir + "/../bin/" + name, dir + "/../libexec/" + name }) {
        if (QFileInfo(candidate).isExecutable()) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

QString displayNameSetting()
{
    QString name = APPLICATION->settings()->get("FriendsDisplayName").toString().trimmed();
    if (name.isEmpty()) {
        if (auto account = APPLICATION->accounts()->defaultAccount()) {
            name = account->profileName();
        }
    }
    return name;
}
}  // namespace

// ---------------- FriendsService ----------------

FriendsService* FriendsService::instance()
{
    static QPointer<FriendsService> s;
    if (!s) {
        s = new FriendsService(QCoreApplication::instance());
    }
    return s;
}

FriendsService::FriendsService(QObject* parent) : QObject(parent)
{
    // Tell friends what we're playing; the sidecar itself reports "hosting" when a LAN world is open.
    connect(APPLICATION, &Application::launchStarted, this, [this](MinecraftInstance* inst, LaunchController*) {
        if (!inst) {
            return;
        }
        send({ { "cmd", "set_status" }, { "status", "playing" }, { "game", inst->name() } });
        // No Qt::UniqueConnection here: it asserts with a functor slot, and the worst a duplicate
        // connection can do is send the same "online" status twice.
        connect(inst, &BaseInstance::runningStatusChanged, this, [this](bool running) {
            if (!running) {
                send({ { "cmd", "set_status" }, { "status", "online" } });
            }
        });
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        if (running()) {
            send({ { "cmd", "quit" } });
            m_proc->closeWriteChannel();
            // let it sign off from the DHT so friends see us go offline quickly
            if (!m_proc->waitForFinished(1500)) {
                m_proc->kill();
            }
        }
    });
    start();
}

FriendsService::~FriendsService()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

void FriendsService::start()
{
    const QString exe = sidecarPath();
    if (exe.isEmpty()) {
        m_error = "missing";
        qWarning() << "fashion-net sidecar not found next to" << QCoreApplication::applicationDirPath();
        emit runningChanged();
        return;
    }
    if (!m_proc) {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, &FriendsService::readOutput);
        connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
            for (const auto& line : m_proc->readAllStandardError().split('\n')) {
                if (!line.trimmed().isEmpty()) {
                    qDebug().noquote() << "fashion-net:" << QString::fromUtf8(line);
                }
            }
        });
        connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            qWarning() << "fashion-net exited with" << code;
            emit runningChanged();
            if (QCoreApplication::closingDown()) {
                return;
            }
            // crashed: restart a few times with backoff
            if (m_restarts < 5) {
                QTimer::singleShot(2000 * (1 << m_restarts++), this, &FriendsService::start);
            } else {
                m_error = "crashed";
                emit runningChanged();
            }
        });
        connect(m_proc, &QProcess::started, this, [this] {
            m_error.clear();
            emit runningChanged();
        });
    }
    const QString data = QDir(APPLICATION->dataRoot()).filePath("fashion-net");
    QDir().mkpath(data);
    QStringList args{ "--data", data, "--name", displayNameSetting() };
    const QString boot = qEnvironmentVariable("FASHION_NET_BOOTSTRAP");
    if (!boot.isEmpty()) {
        args << "--bootstrap" << boot;  // private test DHT
    }
    m_proc->start(exe, args);
}

void FriendsService::readOutput()
{
    m_buffer += m_proc->readAllStandardOutput();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        const auto doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            continue;
        }
        const QVariantMap ev = doc.object().toVariantMap();
        const QString kind = ev.value("ev").toString();
        if (kind == "state") {
            m_restarts = 0;
            m_state = ev;
            runAutomation();
            emit stateChanged();
        } else if (kind == "notice" || kind == "invite_sent") {
            emit notice(ev);
        }
    }
}

// Test automation (like FFPRISM_CREATE): FFPRISM_FRIENDS_AUTO=invite:<instanceId> invites every connected
// friend once; accept:<instanceId> accepts the first invite and joins with that instance.
void FriendsService::runAutomation()
{
    static const QString spec = qEnvironmentVariable("FFPRISM_FRIENDS_AUTO");
    if (spec.isEmpty() || m_autoDone) {
        return;
    }
    const QString action = spec.section(':', 0, 0);
    const QString inst = spec.section(':', 1);
    if (action == "invite") {
        QStringList keys;
        for (const auto& f : m_state.value("friends").toList()) {
            if (f.toMap().value("conn").toString() == "direct") {
                keys << f.toMap().value("key").toString();
            }
        }
        auto* instance = APPLICATION->instances()->getInstanceById(inst);
        if (keys.isEmpty() || !instance) {
            return;
        }
        m_autoDone = true;
        send({ { "cmd", "invite" },
               { "keys", keys },
               { "invite", QVariantMap{ { "instanceId", inst }, { "instance", instance->name() }, { "mc", instance->getPackProfile()->getComponentVersion("net.minecraft") } } } });
    } else if (action == "accept") {
        const auto invites = m_state.value("invites").toList();
        if (invites.isEmpty()) {
            return;
        }
        m_autoDone = true;
        const auto inv = invites.first().toMap();
        joinInstances.insert(inv.value("id").toString(), inst);
        send({ { "cmd", "reply_invite" }, { "from", inv.value("from") }, { "id", inv.value("id") }, { "accept", true } });
    }
}

void FriendsService::send(const QVariantMap& cmd)
{
    if (!running()) {
        return;
    }
    QByteArray line = QJsonDocument(QJsonObject::fromVariantMap(cmd)).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_proc->write(line);
}

// ---------------- FriendsBridge ----------------

FriendsBridge::FriendsBridge(QObject* parent) : QObject(parent)
{
    auto settings = APPLICATION->settings();
    if (!settings->contains("FriendsDisplayName")) {
        settings->registerSetting("FriendsDisplayName", QString());
    }
    auto* svc = FriendsService::instance();
    connect(svc, &FriendsService::stateChanged, this, &FriendsBridge::changed);
    connect(svc, &FriendsService::runningChanged, this, &FriendsBridge::runningChanged);
    connect(svc, &FriendsService::notice, this, &FriendsBridge::notice);
}

bool FriendsBridge::available() const
{
    return FriendsService::instance()->running();
}
QString FriendsBridge::error() const
{
    return FriendsService::instance()->error();
}
QVariantMap FriendsBridge::me() const
{
    return FriendsService::instance()->state().value("me").toMap();
}
QVariantMap FriendsBridge::net() const
{
    return FriendsService::instance()->state().value("net").toMap();
}
QVariantList FriendsBridge::friends() const
{
    return FriendsService::instance()->state().value("friends").toList();
}
QVariantList FriendsBridge::requests() const
{
    return FriendsService::instance()->state().value("requests").toList();
}
QVariantList FriendsBridge::invites() const
{
    return FriendsService::instance()->state().value("invites").toList();
}
QVariantMap FriendsBridge::hosting() const
{
    return FriendsService::instance()->state().value("hosting").toMap();
}
int FriendsBridge::onlineCount() const
{
    int n = 0;
    for (const auto& f : friends()) {
        n += f.toMap().value("online").toBool() ? 1 : 0;
    }
    return n;
}
QString FriendsBridge::displayName() const
{
    const QString n = me().value("name").toString();
    return n.isEmpty() ? displayNameSetting() : n;
}

void FriendsBridge::addFriend(const QString& codeOrLink)
{
    FriendsService::instance()->send({ { "cmd", "add_friend" }, { "code", codeOrLink.trimmed() } });
}
void FriendsBridge::removeFriend(const QString& key)
{
    FriendsService::instance()->send({ { "cmd", "remove_friend" }, { "key", key } });
}
void FriendsBridge::acceptRequest(const QString& key)
{
    FriendsService::instance()->send({ { "cmd", "accept_request" }, { "key", key } });
}
void FriendsBridge::declineRequest(const QString& key)
{
    FriendsService::instance()->send({ { "cmd", "decline_request" }, { "key", key } });
}
void FriendsBridge::setDisplayName(const QString& name)
{
    const QString n = name.trimmed().left(32);
    APPLICATION->settings()->set("FriendsDisplayName", n);
    FriendsService::instance()->send({ { "cmd", "set_name" }, { "name", n.isEmpty() ? displayNameSetting() : n } });
}
void FriendsBridge::invite(const QStringList& keys, const QVariantMap& invite)
{
    FriendsService::instance()->send({ { "cmd", "invite" }, { "keys", keys }, { "invite", invite } });
}
void FriendsBridge::cancelInvite(const QString& id)
{
    FriendsService::instance()->send({ { "cmd", "cancel_invite" }, { "id", id } });
}
void FriendsBridge::replyInvite(const QString& from, const QString& id, bool accept)
{
    FriendsService::instance()->send({ { "cmd", "reply_invite" }, { "from", from }, { "id", id }, { "accept", accept } });
}
void FriendsBridge::retry()
{
    FriendsService::instance()->send({ { "cmd", "retry" } });
}
void FriendsBridge::copyText(const QString& text) const
{
    QGuiApplication::clipboard()->setText(text);
}

bool FriendsBridge::connectInstance(const QString& instanceId, int port)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst || port <= 0 || inst->isRunning()) {
        return false;
    }
    auto target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(QString("127.0.0.1:%1").arg(port), false));
    return APPLICATION->launch(inst, LaunchMode::Normal, target);
}

bool FriendsBridge::looksLikeCode(const QString& text) const
{
    QString t = text.trimmed();
    if (t.contains("fashion://friend/")) {
        return true;
    }
    t.remove(QRegularExpression("[^A-Za-z0-9]"));
    return t.size() == 52;
}

void FriendsBridge::setJoinInstance(const QString& inviteId, const QString& instanceId)
{
    FriendsService::instance()->joinInstances.insert(inviteId, instanceId);
    emit changed();
}

QString FriendsBridge::joinInstance(const QString& inviteId) const
{
    return FriendsService::instance()->joinInstances.value(inviteId).toString();
}
