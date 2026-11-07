// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/**
 * The crash doctor's rule matcher. Pure text in, findings out: no launcher state, so it is unit-tested
 * with real log snippets (tests/CrashRules_test.cpp).
 *
 * It only reports causes a log states outright: the mod loader's own dependency resolution (it computed
 * exactly which mod needs what), the JVM's out-of-memory and class-version errors, GLFW/OpenGL driver
 * errors and unreadable mod archives. Anything else — mixin failures, stack traces through mod packages,
 * "conflict" wording, NPEs — is NOT a diagnosis: the result then has no findings, only the excerpt
 * around the first exception and, under a caveat, the mods the stack trace mentions.
 */
namespace CrashRules {

struct Finding {
    /// "missing-dependency" | "wrong-dependency" | "incompatible" | "duplicate" | "memory" | "memory-reserve" |
    /// "memory-system" | "java" | "graphics" | "corrupt-file"
    QString kind;
    /// Log lines quoted verbatim (only trimmed), the reason this finding exists
    QStringList evidence;
    /// Dependency findings: the mod that needs something, what it needs, the range and what is installed
    QString mod;         ///< display name or id of the mod that declares the dependency
    QString dependency;  ///< id of the needed / conflicting mod ("fabric-api", "minecraft", ...)
    QString dependencyName;
    QString range;  ///< as the loader worded it ("any version", "[9.1,)")
    QString found;  ///< installed version, "" when missing
    /// Duplicate / corrupt findings: mod files named by the log (file names only)
    QStringList files;
    /// Java findings: the Java major version the log says is required; javaTooNew = the game needs an older Java
    int javaMajor = 0;
    int javaCurrent = 0;
    bool javaTooNew = false;
};

struct Diagnosis {
    QList<Finding> findings;  ///< empty = cause unknown
    QStringList excerpt;      ///< lines around the first exception (or the crash report)
    int excerptStart = -1;    ///< index of excerpt's first line in the input
    /// Mods/jars that appear in the stack trace. NOT causes — shown only under a caveat.
    QStringList mentioned;
};

Diagnosis diagnose(const QStringList& lines);
inline Diagnosis diagnose(const QString& text)
{
    return diagnose(text.split('\n'));
}

/// Class file version (52, 61, 65...) to Java major version (8, 17, 21...)
int javaForClassVersion(int classVersion);

QVariantMap toVariant(const Finding& finding);
QVariantMap toVariant(const Diagnosis& diagnosis);

}  // namespace CrashRules
