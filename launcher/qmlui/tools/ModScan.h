// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * What the pre-launch checks need to know about one mod jar, read straight from its metadata files
 * (fabric.mod.json, quilt.mod.json, META-INF/mods.toml, META-INF/neoforge.mods.toml) and from the
 * jars nested inside it. Results are cached per file (path + size + modification time).
 */
namespace ModScan {

/// One metadata file inside the jar.
struct Descriptor {
    bool present = false;
    bool parsed = false;     // false when the file exists but couldn't be read: nothing below is trustworthy
    QStringList ids;         // mod ids it declares ([[mods]] can declare several)
    QSet<QString> provides;  // extra ids it answers to
    QString name;
    QString version;
    QStringList depends;     // required mod ids, as written
    /// Minecraft requirement: Fabric/Quilt style (string or array of strings) or Maven range (Forge/NeoForge)
    QJsonValue mcPredicate;
    QString mcRange;
};

struct Jar {
    bool readable = false;
    Descriptor fabric, quilt, forge, neoforge;
    QSet<QString> nestedIds;  // every id declared by jars bundled inside this one (any depth)
};

/// Reads one jar (cached). Thread-safe.
Jar scan(const QString& path);

/// Fabric/Quilt version predicate against a release version like "1.21.1".
/// Returns 1 = matches, 0 = doesn't, -1 = can't tell for sure.
int matchFabric(const QJsonValue& predicate, const QString& version);
/// Maven range like "[1.20.1,1.21)" against a release version; same return values.
/// A bare version ("1.20.1") is only a recommendation, so it returns -1.
int matchMaven(const QString& range, const QString& version);
/// Human wording of a requirement, e.g. "1.20.x", "1.20.5 or newer".
QString describeFabric(const QJsonValue& predicate);
QString describeMaven(const QString& range);
/// true for plain release numbers like 1.21 or 1.20.1
bool isRelease(const QString& version);

}  // namespace ModScan
