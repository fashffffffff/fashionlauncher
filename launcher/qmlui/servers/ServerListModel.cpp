// SPDX-License-Identifier: GPL-3.0-only
#include "ServerListModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include <io/stream_reader.h>
#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>
#include <sstream>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "ui/pages/instance/McClient.h"
#include "ui/pages/instance/McResolver.h"

namespace {

constexpr int PING_TIMEOUT_MS = 7000;

// ---- MOTD: Minecraft text components and legacy § codes → a small HTML subset Text.StyledText understands

QString colorForName(const QString& name)
{
    static const QHash<QString, QString> colors{
        { "black", "#000000" },       { "dark_blue", "#0000AA" },    { "dark_green", "#00AA00" }, { "dark_aqua", "#00AAAA" },
        { "dark_red", "#AA0000" },    { "dark_purple", "#AA00AA" },  { "gold", "#FFAA00" },       { "gray", "#AAAAAA" },
        { "dark_gray", "#555555" },   { "blue", "#5555FF" },         { "green", "#55FF55" },      { "aqua", "#55FFFF" },
        { "red", "#FF5555" },         { "light_purple", "#FF55FF" }, { "yellow", "#FFFF55" },     { "white", "#FFFFFF" },
    };
    if (name.startsWith('#'))
        return name;
    return colors.value(name);
}

QString colorForCode(QChar c)
{
    static const char* names[] = { "black", "dark_blue", "dark_green", "dark_aqua", "dark_red", "dark_purple", "gold", "gray",
                                   "dark_gray", "blue", "green", "aqua", "red", "light_purple", "yellow", "white" };
    int i = QString("0123456789abcdef").indexOf(c.toLower());
    return i >= 0 ? colorForName(names[i]) : QString();
}

struct Style {
    QString color;
    bool bold = false;
    bool italic = false;
};

struct MotdBuilder {
    QString html;
    QString plain;

    void emitText(const QString& text, const Style& st)
    {
        if (text.isEmpty())
            return;
        plain += text;
        QString esc = text.toHtmlEscaped();
        esc.replace('\n', "<br>");
        if (st.bold)
            esc = "<b>" + esc + "</b>";
        if (st.italic)
            esc = "<i>" + esc + "</i>";
        // Black and the darkest blue are unreadable on dark panels; leave those to the theme's text color.
        if (!st.color.isEmpty() && st.color != "#000000" && st.color != "#0000AA")
            esc = QString("<font color=\"%1\">%2</font>").arg(st.color, esc);
        html += esc;
    }

    // Plain string that may contain § formatting codes
    void legacy(const QString& text, Style st)
    {
        QString run;
        for (int i = 0; i < text.size(); i++) {
            if (text[i] == QChar(0xA7) && i + 1 < text.size()) {
                emitText(run, st);
                run.clear();
                QChar code = text[++i].toLower();
                if (QString("0123456789abcdef").contains(code)) {
                    st = Style{ colorForCode(code) };
                } else if (code == 'l') {
                    st.bold = true;
                } else if (code == 'o') {
                    st.italic = true;
                } else if (code == 'r') {
                    st = Style{};
                }
                continue;
            }
            run += text[i];
        }
        emitText(run, st);
    }

    void component(const QJsonValue& v, Style st)
    {
        if (v.isString()) {
            legacy(v.toString(), st);
            return;
        }
        if (v.isArray()) {
            for (const auto& c : v.toArray())
                component(c, st);
            return;
        }
        if (!v.isObject())
            return;
        auto o = v.toObject();
        if (o.contains("color"))
            st.color = colorForName(o["color"].toString());
        if (o.contains("bold"))
            st.bold = o["bold"].toBool();
        if (o.contains("italic"))
            st.italic = o["italic"].toBool();
        legacy(o["text"].toString(), st);
        if (o.contains("extra"))
            component(o["extra"], st);
    }
};

// ---- servers.dat

std::unique_ptr<nbt::tag_compound> readServersDat(const QString& filename)
{
    try {
        auto res = FS::read(filename);
        if (!res)
            return nullptr;
        const auto& input = res.value();
        std::istringstream in(std::string(input.constData(), input.size()));
        auto pair = nbt::io::read_compound(in);
        if (!pair.first.empty() || !pair.second)
            return nullptr;
        return std::move(pair.second);
    } catch (...) {
        return nullptr;
    }
}

QString tagString(nbt::tag_compound& tag, const char* key)
{
    if (!tag.has_key(key, nbt::tag_type::String))
        return {};
    return QString::fromStdString(tag.at(key).as<nbt::tag_string>().get());
}

}  // namespace

ServerListModel::ServerListModel(QObject* parent) : QAbstractListModel(parent) {}

ServerListModel::~ServerListModel() = default;

int ServerListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_servers.size();
}

QHash<int, QByteArray> ServerListModel::roleNames() const
{
    return { { NameRole, "name" },       { AddressRole, "address" },       { IconRole, "icon" },
             { StatusRole, "status" },   { PlayersRole, "players" },       { MaxPlayersRole, "maxPlayers" },
             { MotdRole, "motd" },       { MotdPlainRole, "motdPlain" },   { LatencyRole, "latency" },
             { VersionRole, "version" }, { ErrorRole, "error" } };
}

QVariant ServerListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_servers.size())
        return {};
    const auto& s = m_servers[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return s.name;
        case AddressRole:
            return s.address;
        case IconRole:
            return s.icon.isEmpty() ? QString() : QString("data:image/png;base64,") + QString::fromLatin1(s.icon.toBase64());
        case StatusRole:
            return s.status;
        case PlayersRole:
            return s.players;
        case MaxPlayersRole:
            return s.maxPlayers;
        case MotdRole:
            return s.motd;
        case MotdPlainRole:
            return s.motdPlain;
        case LatencyRole:
            return s.latency;
        case VersionRole:
            return s.version;
        case ErrorRole:
            return s.error;
        default:
            return {};
    }
}

void ServerListModel::setInstanceId(const QString& id)
{
    if (id == m_instanceId)
        return;
    m_instanceId = id;
    emit instanceIdChanged();
    reload();
}

QString ServerListModel::serversPath() const
{
    auto* inst = APPLICATION->instances()->getInstanceById(m_instanceId);
    if (!inst)
        return {};
    return FS::PathCombine(inst->gameRoot(), "servers.dat");
}

void ServerListModel::reload()
{
    beginResetModel();
    m_servers.clear();
    // Pings in flight belong to the old list; their tokens no longer match anything.
    if (m_pending) {
        m_pending = 0;
        emit pingingChanged();
    }
    auto path = serversPath();
    if (!path.isEmpty()) {
        if (auto root = readServersDat(path); root && root->has_key("servers", nbt::tag_type::List)) {
            try {
                for (auto& item : root->at("servers").as<nbt::tag_list>()) {
                    auto& tag = item.as<nbt::tag_compound>();
                    Entry e;
                    e.name = tagString(tag, "name");
                    e.address = tagString(tag, "ip");
                    auto icon = tagString(tag, "icon");
                    if (!icon.isEmpty())
                        e.icon = QByteArray::fromBase64(icon.toLatin1());
                    e.raw = std::make_shared<nbt::tag_compound>(tag);
                    m_servers.append(e);
                }
            } catch (const std::exception& ex) {
                qWarning() << "Couldn't read" << path << ex.what();
            }
        }
    }
    endResetModel();
    emit countChanged();
    if (m_autoPing)
        pingAll();
}

bool ServerListModel::save()
{
    auto path = serversPath();
    if (path.isEmpty())
        return false;
    try {
        nbt::tag_list list;
        for (auto& s : m_servers) {
            nbt::tag_compound tag = s.raw ? *s.raw : nbt::tag_compound{};
            tag.put("name", nbt::tag_string(s.name.trimmed().toStdString()));
            tag.put("ip", nbt::tag_string(s.address.trimmed().toStdString()));
            if (!s.icon.isEmpty())
                tag.put("icon", nbt::tag_string(s.icon.toBase64().toStdString()));
            else
                tag.erase("icon");
            list.push_back(std::move(tag));
        }
        nbt::tag_compound root;
        root.insert("servers", nbt::value(std::move(list)));
        std::ostringstream out;
        nbt::io::write_tag("", root, out);
        if (!FS::ensureFilePathExists(path))
            return false;
        auto bytes = out.str();
        if (!FS::write(path, QByteArray(bytes.data(), static_cast<int>(bytes.size())))) {
            emit failed(tr("Couldn't save the server list."));
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        qWarning() << "Couldn't write" << path << ex.what();
        emit failed(tr("Couldn't save the server list."));
        return false;
    }
}

QString ServerListModel::addressProblem(const QString& address) const
{
    auto a = address.trimmed();
    if (a.isEmpty())
        return tr("Type the server address, for example play.example.com");
    if (a.contains(' '))
        return tr("A server address can't contain spaces.");
    static const QRegularExpression portRe(":(\\d+)$");
    if (auto m = portRe.match(a); m.hasMatch() && !a.startsWith('[')) {
        int port = m.captured(1).toInt();
        if (port < 1 || port > 65535)
            return tr("The port after the colon must be between 1 and 65535.");
    }
    return {};
}

bool ServerListModel::add(const QString& name, const QString& address)
{
    if (!addressProblem(address).isEmpty())
        return false;
    Entry e;
    e.name = name.trimmed().isEmpty() ? tr("Minecraft Server") : name.trimmed();
    e.address = address.trimmed();
    beginInsertRows({}, m_servers.size(), m_servers.size());
    m_servers.append(e);
    endInsertRows();
    emit countChanged();
    save();
    ping(m_servers.size() - 1);
    return true;
}

bool ServerListModel::edit(int row, const QString& name, const QString& address)
{
    if (row < 0 || row >= m_servers.size() || !addressProblem(address).isEmpty())
        return false;
    auto& s = m_servers[row];
    bool addressChanged = s.address != address.trimmed();
    s.name = name.trimmed().isEmpty() ? tr("Minecraft Server") : name.trimmed();
    s.address = address.trimmed();
    if (addressChanged)
        s.icon.clear();
    touch(row);
    save();
    if (addressChanged)
        ping(row);
    return true;
}

void ServerListModel::remove(int row)
{
    if (row < 0 || row >= m_servers.size())
        return;
    beginRemoveRows({}, row, row);
    m_servers.removeAt(row);
    endRemoveRows();
    emit countChanged();
    save();
}

void ServerListModel::move(int from, int to)
{
    if (from < 0 || from >= m_servers.size() || to < 0 || to >= m_servers.size() || from == to)
        return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_servers.move(from, to);
    endMoveRows();
    save();
}

void ServerListModel::touch(int row)
{
    emit dataChanged(index(row), index(row));
}

void ServerListModel::pingAll()
{
    for (int i = 0; i < m_servers.size(); i++)
        ping(i);
}

void ServerListModel::finishPing(quint64 token, const std::function<void(Entry&)>& apply)
{
    for (int i = 0; i < m_servers.size(); i++) {
        if (m_servers[i].pingToken != token)
            continue;
        m_servers[i].pingToken = 0;
        apply(m_servers[i]);
        touch(i);
        if (m_pending > 0 && --m_pending == 0)
            emit pingingChanged();
        return;
    }
}

void ServerListModel::ping(int row)
{
    if (row < 0 || row >= m_servers.size())
        return;
    auto& s = m_servers[row];
    if (s.pingToken == 0) {
        if (m_pending++ == 0)
            emit pingingChanged();
    }
    const quint64 token = m_nextToken++;
    s.pingToken = token;
    s.status = "pinging";
    s.error.clear();
    touch(row);

    auto target = MinecraftTarget::parse(s.address, false);
    const QString domain = target.address;

    // One guard object owns the whole attempt; deleting it ends it (timeout, success or failure).
    auto* guard = new QObject(this);
    auto fail = [this, token, guard](const QString& why) {
        finishPing(token, [why](Entry& e) {
            e.status = "offline";
            e.players = e.maxPlayers = e.latency = -1;
            e.error = why;
        });
        guard->deleteLater();
    };

    auto* timeout = new QTimer(guard);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, guard, [fail] { fail(tr("The server didn't answer in time.")); });
    timeout->start(PING_TIMEOUT_MS);

    auto* resolver = new McResolver(guard, domain, target.port);
    connect(resolver, &McResolver::failed, guard, [fail](const QString&) { fail(tr("Couldn't find a server at this address.")); });
    connect(resolver, &McResolver::succeeded, guard, [this, guard, token, domain, fail](QString ip, int port) {
        auto* client = new McClient(guard, domain, ip, static_cast<uint16_t>(port));
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        connect(client, &McClient::failed, guard, [fail](const QString&) { fail(tr("The server is offline or not reachable.")); });
        connect(client, &McClient::succeeded, guard, [this, guard, token, clock](QJsonObject data) {
            // The status request is one round trip after the TCP handshake; half the total is a fair latency estimate.
            const int ms = static_cast<int>(clock->elapsed() / 2);
            finishPing(token, [&data, ms](Entry& e) {
                e.status = "online";
                e.latency = std::max(1, ms);
                auto players = data["players"].toObject();
                e.players = players["online"].toInt(-1);
                e.maxPlayers = players["max"].toInt(-1);
                e.version = data["version"].toObject()["name"].toString();
                MotdBuilder b;
                b.component(data["description"], {});
                e.motd = b.html.trimmed();
                e.motdPlain = b.plain.trimmed();
                auto favicon = data["favicon"].toString();
                if (auto comma = favicon.indexOf(','); favicon.startsWith("data:image/png;base64,") && comma > 0)
                    e.icon = QByteArray::fromBase64(favicon.mid(comma + 1).toLatin1());
                e.error.clear();
            });
            guard->deleteLater();
        });
        client->getStatusData();
    });
    resolver->ping();
}

bool ServerListModel::join(int row)
{
    if (row < 0 || row >= m_servers.size())
        return false;
    auto* inst = APPLICATION->instances()->getInstanceById(m_instanceId);
    if (!inst) {
        emit failed(tr("This instance no longer exists."));
        return false;
    }
    if (inst->isRunning()) {
        emit failed(tr("The game is already running. Join the server from the in-game Multiplayer menu."));
        return false;
    }
    auto target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(m_servers[row].address, false));
    return APPLICATION->launch(inst, LaunchMode::Normal, target);
}
