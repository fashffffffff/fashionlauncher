// SPDX-License-Identifier: GPL-3.0-only
#include "QuickPlayBridge.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/Mode.h"
#include "minecraft/World.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "qmlui/LauncherBridge.h"
#include "qmlui/servers/ServerListModel.h"
#include "settings/SettingsObject.h"

QuickPlayBridge* QuickPlayBridge::s_instance = nullptr;

namespace {

const char* kLastSetting = "QmlLastQuickPlay";

MinecraftInstance* findInstance(const QString& id)
{
    return APPLICATION->instances()->getInstanceById(id);
}

struct Support {
    bool world = false;
    bool server = false;
};

Support supportOf(MinecraftInstance* inst)
{
    Support s;
    if (!inst) {
        return s;
    }
    auto* profile = inst->getPackProfile();
    // Components load lazily; the game version is needed even before anything opened the instance.
    if (!profile->getComponent("net.minecraft")) {
        if (auto res = profile->reload(Net::Mode::Offline); !res) {
            qWarning() << "Quick Play: failed to load components of" << inst->id() << res.error();
        }
    }
    auto component = profile->getComponent("net.minecraft");
    if (!component) {
        return s;
    }
    QSet<QString> traits;
    if (component->getVersionFile()) {
        traits = inst->traits();
    } else {
        // Its metadata is still being resolved in the background: read the traits from the cached meta file.
        QFile file(QDir("meta").absoluteFilePath("net.minecraft/" + component->getVersion() + ".json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return s;
        }
        for (const auto& trait : QJsonDocument::fromJson(file.readAll()).object().value("+traits").toArray()) {
            traits.insert(trait.toString());
        }
    }
    const bool legacy = traits.contains("legacyLaunch") || traits.contains("alphaLaunch");
    s.world = traits.contains("feature:is_quick_play_singleplayer");
    s.server = traits.contains("feature:is_quick_play_multiplayer") || !legacy;
    return s;
}

/// "host" for the default port, "host:port" otherwise, lower-case; used to match servers.dat entries
QString normalAddress(const QString& address)
{
    const auto t = MinecraftTarget::parse(address.trimmed(), false);
    const QString host = t.address.toLower();
    return t.port == 25565 ? host : host + ':' + QString::number(t.port);
}

QList<World> readWorlds(MinecraftInstance* inst)
{
    QList<World> out;
    QDir saves(QDir(inst->gameRoot()).filePath("saves"));
    for (const auto& info : saves.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        World w(info);
        if (w.isValid()) {
            w.loadMetadata();  // level.dat is read lazily
        }
        if (w.isValid() && !info.fileName().startsWith('.')) {
            out.append(w);
        }
    }
    std::sort(out.begin(), out.end(), [](const World& a, const World& b) { return a.lastPlayed() > b.lastPlayed(); });
    return out;
}

struct Candidate {
    QString kind;
    QString value;
    QDateTime time;
};

/// The last thing the game itself did according to logs/latest.log: a server it connected to, or empty when the
/// integrated (singleplayer) server started after the last connection.
Candidate fromLog(MinecraftInstance* inst)
{
    QFile file(QDir(inst->gameRoot()).filePath("logs/latest.log"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const qint64 maxBytes = 8 * 1024 * 1024;
    if (file.size() > maxBytes) {
        file.seek(file.size() - maxBytes);
    }
    const QString text = QString::fromUtf8(file.readAll());
    static const QRegularExpression connectRe(QStringLiteral(R"(\]: Connecting to ([^\s,]+), (\d+)\s*$)"),
                                              QRegularExpression::MultilineOption);
    static const QRegularExpression integratedRe(QStringLiteral(R"(\]: Starting integrated minecraft server)"));
    qsizetype connectAt = -1;
    QRegularExpressionMatch lastConnect;
    for (auto it = connectRe.globalMatch(text); it.hasNext();) {
        lastConnect = it.next();
        connectAt = lastConnect.capturedStart();
    }
    if (connectAt < 0 || text.indexOf(integratedRe, connectAt) >= 0) {
        return {};
    }
    QString address = lastConnect.captured(1);
    if (lastConnect.captured(2) != "25565") {
        address += ':' + lastConnect.captured(2);
    }
    return { "server", address, QFileInfo(file).lastModified() };
}

Candidate recorded(MinecraftInstance* inst)
{
    auto settings = inst->settings();
    if (!settings->contains(kLastSetting)) {
        return {};
    }
    const auto obj = QJsonDocument::fromJson(settings->get(kLastSetting).toString().toUtf8()).object();
    return { obj.value("kind").toString(), obj.value("value").toString(),
             QDateTime::fromMSecsSinceEpoch(qint64(obj.value("time").toDouble())) };
}

void record(MinecraftInstance* inst, const QString& kind, const QString& value)
{
    auto settings = inst->settings();
    if (!settings->contains(kLastSetting)) {
        settings->registerSetting(kLastSetting, QString());
    }
    QJsonObject obj{ { "kind", kind }, { "value", value }, { "time", double(QDateTime::currentMSecsSinceEpoch()) } };
    settings->set(kLastSetting, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

QString worldIcon(const World& w)
{
    return w.iconFile().isEmpty() ? QString() : QUrl::fromLocalFile(w.iconFile()).toString();
}

}  // namespace

QuickPlayBridge::QuickPlayBridge(LauncherBridge* launcher, QObject* parent) : QObject(parent), m_launcher(launcher)
{
    s_instance = this;
}

QVariantMap QuickPlayBridge::support(const QString& id) const
{
    const auto s = supportOf(findInstance(id));
    return { { "world", s.world }, { "server", s.server } };
}

QVariantList QuickPlayBridge::worlds(const QString& id) const
{
    QVariantList out;
    auto* inst = findInstance(id);
    if (!supportOf(inst).world) {
        return out;
    }
    for (const auto& w : readWorlds(inst)) {
        out.append(QVariantMap{ { "folder", w.folderName() },
                                { "name", w.name().isEmpty() ? w.folderName() : w.name() },
                                { "icon", worldIcon(w) },
                                { "lastPlayed", w.lastPlayed().toMSecsSinceEpoch() } });
    }
    return out;
}

QVariantList QuickPlayBridge::servers(const QString& id) const
{
    QVariantList out;
    if (!supportOf(findInstance(id)).server) {
        return out;
    }
    ServerListModel model;
    model.setInstanceId(id);
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto idx = model.index(row);
        const QString address = idx.data(ServerListModel::AddressRole).toString();
        if (address.trimmed().isEmpty()) {
            continue;
        }
        out.append(QVariantMap{ { "name", idx.data(ServerListModel::NameRole).toString() },
                                { "address", address },
                                { "icon", idx.data(ServerListModel::IconRole).toString() } });
    }
    return out;
}

QVariantMap QuickPlayBridge::last(const QString& id) const
{
    auto* inst = findInstance(id);
    const auto s = supportOf(inst);
    if (!s.world && !s.server) {
        return {};
    }
    const QList<World> saves = s.world ? readWorlds(inst) : QList<World>();
    auto findWorld = [&saves](const QString& folder) -> const World* {
        for (const auto& w : saves) {
            if (w.folderName() == folder) {
                return &w;
            }
        }
        return nullptr;
    };

    QList<Candidate> candidates;
    if (!saves.isEmpty()) {
        candidates.append({ "world", saves.first().folderName(), saves.first().lastPlayed() });
    }
    candidates.append(fromLog(inst));
    candidates.append(recorded(inst));

    const Candidate* best = nullptr;
    for (const auto& c : candidates) {
        const bool usable = (c.kind == "world" && s.world && findWorld(c.value)) ||
                            (c.kind == "server" && s.server && !c.value.trimmed().isEmpty());
        if (usable && c.time.isValid() && (!best || c.time > best->time)) {
            best = &c;
        }
    }
    if (!best) {
        return {};
    }
    if (best->kind == "world") {
        const World* w = findWorld(best->value);
        return { { "kind", "world" },
                 { "value", w->folderName() },
                 { "name", w->name().isEmpty() ? w->folderName() : w->name() },
                 { "icon", worldIcon(*w) } };
    }
    QVariantMap out{ { "kind", "server" }, { "value", best->value }, { "name", best->value }, { "icon", QString() } };
    const QString wanted = normalAddress(best->value);
    for (const auto& entry : servers(id)) {
        const auto server = entry.toMap();
        if (normalAddress(server.value("address").toString()) == wanted) {
            if (!server.value("name").toString().trimmed().isEmpty()) {
                out["name"] = server.value("name");
            }
            out["icon"] = server.value("icon");
            break;
        }
    }
    return out;
}

bool QuickPlayBridge::play(const QString& id, const QString& kind, const QString& value)
{
    auto* inst = findInstance(id);
    const auto s = supportOf(inst);
    if (!inst || value.trimmed().isEmpty() || inst->isRunning()) {
        return false;
    }
    std::shared_ptr<MinecraftTarget> target;
    if (kind == "world" && s.world) {
        target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(value, true));
    } else if (kind == "server" && s.server) {
        target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(value.trimmed(), false));
    } else {
        return false;
    }
    record(inst, kind, value.trimmed());
    return m_launcher->launchTo(id, target);
}
