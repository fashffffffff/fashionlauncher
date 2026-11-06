// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QString>

class BaseInstance;

/**
 * Recommended settings that a modpack author ships inside the pack as "fashion-launcher.json"
 * (see docs/MODPACK_SETTINGS.md):
 *   { "memory": { "min": 2048, "max": 8192 }, "jvmArgs": "-XX:+UseZGC", "notes": "…" }
 */
namespace PackSettings {

inline constexpr auto FileName = "fashion-launcher.json";

/// The settings file at the root of a .zip/.mrpack, or an empty object.
QJsonObject readFromArchive(const QString& archivePath);
/// Applies the settings to a freshly imported instance. Returns a sentence for the user
/// ("This modpack set 8 GB of memory."), or "" when nothing was applied.
QString apply(BaseInstance* instance, const QJsonObject& settings);
/// The instance's own memory/JVM settings in the same format (empty when it uses the defaults).
QJsonObject fromInstance(BaseInstance* instance);
/// Writes the file into an existing archive (replacing an older copy). Returns an error or "".
QString writeIntoArchive(const QString& archivePath, const QJsonObject& settings);
/// "8 GB", "3.5 GB", "512 MB"
QString formatMemory(int mib);

}  // namespace PackSettings
