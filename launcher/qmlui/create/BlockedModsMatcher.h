// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>

#include "ui/dialogs/BlockedModsDialog.h"

/**
 * Pure, non-UI matching for the blocked-mods question: which local file belongs to which entry.
 * A faithful port of the matching inside BlockedModsDialog (which keeps its own copy, so classic
 * behaviour cannot change); used by the QML bridge and covered by a unit test.
 */
namespace BlockedModsMatcher {

/// Case-insensitive file name equality.
bool sameName(const QString& fileName, const QString& modName);

/// Name equality ignoring case and treating '-', '+', '.' and '_' as interchangeable separators.
bool laxSameName(const QString& fileName, const QString& modName);

/// If path names an entry that has no hash, that entry is matched by name alone. Returns true
/// when the file names an entry and should be hashed (to confirm or extend the match).
bool matchByName(QList<BlockedMod>& mods, const QString& path, const QString& downloadDir, bool moveFromDownloads);

/// Mark an entry found when its hash matches (case-insensitive). Returns true on a match.
bool matchByHash(QList<BlockedMod>& mods, const QString& hash, const QString& path, const QString& downloadDir, bool moveFromDownloads);

/// True when every entry has been found.
bool allFound(const QList<BlockedMod>& mods);

}  // namespace BlockedModsMatcher
