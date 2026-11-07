// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QVariantMap>

class LauncherBridge;
class MinecraftInstance;

/**
 * "Continue": start an instance straight in a world or on a server (Quick Play). Exposed to QML as "QuickPlay".
 *
 * Which game versions can do what:
 *  - world: needs the `feature:is_quick_play_singleplayer` trait from the version metadata (1.20 / 23w14a and newer,
 *    passed as --quickPlaySingleplayer). Older versions have no way to open a world from the command line.
 *  - server: every non-legacy version. 1.20+ gets --quickPlayMultiplayer, older ones the old --server/--port pair.
 *    Legacy (applet/alpha) launches ignore both, so they get nothing.
 *
 * The last target of an instance is the newest of: the world with the latest LastPlayed in level.dat, the server
 * the game itself last connected to (the last "Connecting to host, port" line in logs/latest.log, unless the
 * integrated server started after it), and the target the launcher last launched with (instance setting
 * `QmlLastQuickPlay`, a fallback for when the log is gone). The instance's join-on-launch settings are never touched.
 */
class QuickPlayBridge : public QObject {
    Q_OBJECT

   public:
    explicit QuickPlayBridge(LauncherBridge* launcher, QObject* parent = nullptr);
    static QuickPlayBridge* instance() { return s_instance; }

    /// { world: bool, server: bool }
    Q_INVOKABLE QVariantMap support(const QString& id) const;
    /// { kind: "world" | "server", value: folder or address, name, icon } or {} when unknown or unsupported
    Q_INVOKABLE QVariantMap last(const QString& id) const;
    /// [{ folder, name, icon, lastPlayed }] newest first; empty when worlds are unsupported
    Q_INVOKABLE QVariantList worlds(const QString& id) const;
    /// [{ name, address, icon }] in servers.dat order; empty when servers are unsupported
    Q_INVOKABLE QVariantList servers(const QString& id) const;
    /// Launch once into kind "world" (value = save folder) or "server" (value = address[:port])
    Q_INVOKABLE bool play(const QString& id, const QString& kind, const QString& value);

   private:
    static QuickPlayBridge* s_instance;
    LauncherBridge* m_launcher;
};
