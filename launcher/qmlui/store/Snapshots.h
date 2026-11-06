// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariantList>

class MinecraftInstance;

/**
 * Launch snapshots: <instance>/.fashion/snapshots/<yyyyMMdd-HHmmss>.json.
 * A snapshot records game components and, for every mod / resource pack / shader pack, its file name, sha256 (store
 * key), enabled state and Modrinth/CurseForge ids from Prism's .index metadata. Files are kept by reference: the
 * content store holds them as long as a snapshot names them.
 */
namespace Snapshots {

constexpr int kKeep = 5;

// Content folders by category ("mods", "resourcepacks", "shaderpacks").
QList<QPair<QString, QString>> categories(MinecraftInstance* inst);
QStringList contentDirs(MinecraftInstance* inst);

QString dir(const QString& instanceRoot);

// Builds the manifest of the instance as it is now. shaByPath comes from ContentStore::process.
QJsonObject build(MinecraftInstance* inst, const QHash<QString, QString>& shaByPath);
// The two halves of build(): components need the GUI thread (PackProfile), files can be read anywhere.
QJsonObject components(MinecraftInstance* inst);
QJsonObject files(const QList<QPair<QString, QString>>& cats, const QHash<QString, QString>& shaByPath);

// Writes the manifest unless it matches the newest snapshot; trims to kKeep. Returns the file written or "".
QString write(const QString& instanceRoot, QJsonObject manifest, const QString& reason = {});

QList<QJsonObject> list(const QString& instanceRoot);  // newest first, each with "file"
QJsonObject read(const QString& file);

// All store keys named by snapshots of the given instances.
QSet<QString> referenced(const QStringList& instanceRoots);

// What restoring `snap` would change compared with `now`: per category added / removed / changed counts.
QVariantMap diff(const QJsonObject& snap, const QJsonObject& now);

}  // namespace Snapshots
