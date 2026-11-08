// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

#include "Version.h"

/**
 * Pure helpers for "Move to another Minecraft version": status classification, version deltas and
 * default names. Header-only so the unit tests (tests/Upgrade_test.cpp) can cover them directly.
 */
namespace UpgradeLogic {

/// What happens to one mod when the instance moves to the target Minecraft version.
enum class Status { Ready, Missing, Unknown };

inline const char* statusKey(Status s)
{
    switch (s) {
        case Status::Ready:
            return "ready";
        case Status::Missing:
            return "missing";
        case Status::Unknown:
            return "unknown";
    }
    return "unknown";
}

/// A mod is replaceable ("ready") when it has provider metadata and a compatible version exists for
/// the target Minecraft version and the same loader. Without metadata its fate is unknown.
inline Status classify(bool hasMetadata, bool hasCompatibleVersion)
{
    if (!hasMetadata) {
        return Status::Unknown;
    }
    return hasCompatibleVersion ? Status::Ready : Status::Missing;
}

/// How the target Minecraft version relates to the current one (for the one-way world warning).
enum class Delta { Same, Upgrade, Downgrade, Unknown };

inline Delta compareVersions(const QString& from, const QString& to)
{
    if (from.isEmpty() || to.isEmpty()) {
        return Delta::Unknown;
    }
    const Version a(from);
    const Version b(to);
    if (a == b) {
        return Delta::Same;
    }
    return a < b ? Delta::Upgrade : Delta::Downgrade;
}

/// Default name of the upgraded copy: "<name> <version>".
inline QString defaultName(const QString& baseName, const QString& mcVersion)
{
    const QString base = baseName.trimmed();
    const QString ver = mcVersion.trimmed();
    if (base.isEmpty()) {
        return ver;
    }
    if (ver.isEmpty()) {
        return base;
    }
    return base + " " + ver;
}

/// Loaders keyed the way the QML shell names them ("fabric" | "quilt" | "forge" | "neoforge" | "").
enum class Loader { None, Fabric, Quilt, Forge, NeoForge, Other };

inline Loader loader(const QString& key)
{
    if (key == "fabric") {
        return Loader::Fabric;
    }
    if (key == "quilt") {
        return Loader::Quilt;
    }
    if (key == "forge") {
        return Loader::Forge;
    }
    if (key == "neoforge") {
        return Loader::NeoForge;
    }
    return key.isEmpty() ? Loader::None : Loader::Other;
}

/// True for loaders whose metadata is not tied to a Minecraft version (CreateBridge's fabricLike).
inline bool loaderVersionIndependent(Loader l)
{
    return l == Loader::Fabric || l == Loader::Quilt;
}

/// Loaders that exist at all, for a Minecraft version (pure part of the availability check;
/// Forge/NeoForge are answered from their metadata lists, see UpgradePlanTask).
inline bool loaderSupportsMc(Loader l, const QString& mcVersion)
{
    switch (l) {
        case Loader::None:
            return true;
        case Loader::Fabric:
        case Loader::Quilt:
            // Same rule as Prism: Fabric and Quilt exist for 1.14 and newer, any loader build fits.
            return !mcVersion.isEmpty() && Version(mcVersion) >= Version("1.14");
        case Loader::Forge:
        case Loader::NeoForge:
        case Loader::Other:
            return !mcVersion.isEmpty();
    }
    return false;
}

}  // namespace UpgradeLogic
