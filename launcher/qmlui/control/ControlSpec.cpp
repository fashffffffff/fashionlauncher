// SPDX-License-Identifier: GPL-3.0-only
#include "ControlSpec.h"

#include <QDir>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

struct Arg {
    const char* name;
    const char* type;  // "string" | "integer" | "boolean" | "array"
    const char* description;
    bool required = false;
    QStringList choices = {};
};

QJsonObject tool(const char* name, const char* description, const QList<Arg>& args = {})
{
    QJsonObject props;
    QJsonArray required;
    for (const auto& a : args) {
        QJsonObject p{ { "type", a.type }, { "description", a.description } };
        if (QLatin1String(a.type) == QLatin1String("array")) {
            p["items"] = QJsonObject{ { "type", "string" } };
        }
        if (!a.choices.isEmpty()) {
            p["enum"] = QJsonArray::fromStringList(a.choices);
        }
        props[a.name] = p;
        if (a.required) {
            required.append(a.name);
        }
    }
    QJsonObject schema{ { "type", "object" }, { "properties", props }, { "additionalProperties", false } };
    if (!required.isEmpty()) {
        schema["required"] = required;
    }
    return QJsonObject{ { "name", name }, { "description", description }, { "inputSchema", schema } };
}

const QStringList kKinds{ "mods", "resourcepacks", "shaderpacks" };
const QStringList kPlatforms{ "modrinth", "curseforge" };
const QStringList kLoaders{ "none", "fabric", "quilt", "forge", "neoforge" };

const Arg kInstance{ "instance", "string", "Instance id or exact name (see list_instances)", true };
const Arg kKind{ "kind", "string", "What to work with; default mods", false, kKinds };

QJsonArray build()
{
    QJsonArray t;
    t.append(tool("status",
                  "Launcher overview: version, data folder, the default account, running games and background jobs "
                  "(installs, downloads) with their progress."));
    t.append(tool("list_instances",
                  "All instances (installations of Minecraft): id, name, Minecraft version, mod loader, group, whether it "
                  "is running, total play time."));
    t.append(tool("get_instance", "Details of one instance: versions, loader, memory and Java settings, counts of mods, "
                                  "resource packs, shaders and worlds, folders.",
                  { kInstance }));
    t.append(tool("list_minecraft_versions", "Minecraft versions that can be installed, newest first.",
                  { { "include_snapshots", "boolean", "Also list snapshots (default false)" },
                    { "limit", "integer", "How many to return (default 40)" } }));
    t.append(tool("create_instance",
                  "Create a new instance and download what it needs. Waits until it is ready and returns its id. Mod "
                  "loader versions default to the recommended one for that Minecraft version.",
                  { { "minecraft_version", "string", "Minecraft version, e.g. 1.21.1", true },
                    { "loader", "string", "Mod loader (default none)", false, kLoaders },
                    { "loader_version", "string", "Loader version (default: recommended/latest for that Minecraft version)" },
                    { "name", "string", "Instance name (default: version and loader)" },
                    { "group", "string", "Group to put it in" } }));
    t.append(tool("rename_instance", "Rename an instance.",
                  { kInstance, { "name", "string", "New name", true } }));
    t.append(tool("delete_instance",
                  "Delete an instance with all its worlds. The player has to confirm it in the launcher window; returns "
                  "whether they did.",
                  { kInstance }));
    t.append(tool("set_instance_settings",
                  "Change per-instance settings. Only the given fields change; memory in megabytes.",
                  { kInstance,
                    { "max_memory_mb", "integer", "Maximum Java heap (-Xmx)" },
                    { "min_memory_mb", "integer", "Initial Java heap (-Xms)" },
                    { "java_args", "string", "Extra JVM arguments (empty string clears them)" } }));
    t.append(tool("list_mods", "Installed mods, resource packs or shader packs of an instance: name, version, file, "
                               "enabled, where it came from.",
                  { kInstance, kKind }));
    t.append(tool("search_mods",
                  "Search Modrinth or CurseForge for things that fit the instance (its Minecraft version and loader). "
                  "Returns project ids to pass to install_mods.",
                  { kInstance,
                    { "query", "string", "Search words (empty: most popular)" },
                    kKind,
                    { "platform", "string", "Where to search (default modrinth)", false, kPlatforms },
                    { "limit", "integer", "How many results (default 15)" } }));
    t.append(tool("install_mods",
                  "Install projects found by search_mods (same instance, kind and platform) with their required "
                  "dependencies, one after another. Waits until done and reports each result.",
                  { kInstance,
                    { "project_ids", "array", "Project ids from search_mods", true },
                    kKind,
                    { "platform", "string", "Same platform as the search (default modrinth)", false, kPlatforms } }));
    t.append(tool("set_mod_enabled", "Turn an installed mod (or pack) on or off without removing it.",
                  { kInstance, { "file", "string", "File name from list_mods", true },
                    { "enabled", "boolean", "true = on, false = off", true }, kKind }));
    t.append(tool("remove_mod", "Remove an installed mod (or pack) by moving its file to the system trash.",
                  { kInstance, { "file", "string", "File name from list_mods", true }, kKind }));
    t.append(tool("list_worlds", "Singleplayer worlds of an instance: folder name, name, last played, size.",
                  { kInstance }));
    t.append(tool("launch",
                  "Start the game. Returns right away; use status and get_log to follow it. Optionally join a server or "
                  "open a world directly (Minecraft 1.20+ for worlds).",
                  { kInstance,
                    { "server", "string", "Server address to join, e.g. play.example.org" },
                    { "world", "string", "World folder name to open (see list_worlds)" },
                    { "offline", "boolean", "Play offline with the default account (default false)" } }));
    t.append(tool("stop", "Stop a running game right away (like Kill). Unsaved progress since the last autosave is lost.",
                  { kInstance }));
    t.append(tool("get_log",
                  "The game log of an instance: the live output while it runs, otherwise the last log file. Returns the "
                  "last lines.",
                  { kInstance,
                    { "lines", "integer", "How many last lines (default 200, max 5000)" },
                    { "level", "string", "Only lines at or above this level", false, { "all", "warning", "error" } } }));
    t.append(tool("diagnose_crash",
                  "Explain why the game of an instance crashed, from its last log. Only names causes the log states.",
                  { kInstance }));
    t.append(tool("list_accounts", "Accounts (Microsoft or offline): player name, type, which one is the default. Never "
                                   "returns tokens."));
    t.append(tool("set_default_account", "Make an account the default for playing.",
                  { { "name", "string", "Player name from list_accounts", true } }));
    return t;
}

}  // namespace

namespace ControlSpec {

QJsonArray tools()
{
    static const QJsonArray t = build();
    return t;
}

bool has(const QString& name)
{
    for (const auto& v : tools()) {
        if (v.toObject().value("name").toString() == name) {
            return true;
        }
    }
    return false;
}

QString registryDir()
{
    // per user, outside any data folder, so a client finds launchers started with -d too
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("FashionLauncher-control");
}

}  // namespace ControlSpec
