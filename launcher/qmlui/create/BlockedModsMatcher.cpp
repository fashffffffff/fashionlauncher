// SPDX-License-Identifier: GPL-3.0-only
#include "BlockedModsMatcher.h"

#include <QFileInfo>
#include <algorithm>

namespace BlockedModsMatcher {

bool sameName(const QString& fileName, const QString& modName)
{
    return modName.compare(fileName, Qt::CaseInsensitive) == 0;
}

// Super lax compare (but not fuzzy): lowercase, all separator characters become whitespace,
// whitespace runs simplified to one space — effectively comparing two names ignoring all
// separators and case, so "Some.Mod-2_pack" and "some mod 2 pack" are the same file name.
bool laxSameName(const QString& fileName, const QString& modName)
{
    const QList<QChar> allowedSeparators = { '-', '+', '.', '_' };

    auto fsName = fileName.toLower();
    auto metaName = modName.toLower();
    for (auto sep : allowedSeparators) {
        fsName = fsName.replace(sep, ' ');
        metaName = metaName.replace(sep, ' ');
    }

    return fsName.simplified().compare(metaName.simplified()) == 0;
}

namespace {
void markFound(BlockedMod& mod, const QString& path, const QString& downloadDir, bool moveFromDownloads)
{
    mod.matched = true;
    mod.localPath = path;
    if (moveFromDownloads) {
        mod.move = QFileInfo(path).absoluteFilePath().startsWith(downloadDir);
    }
}
}  // namespace

bool matchByName(QList<BlockedMod>& mods, const QString& path, const QString& downloadDir, bool moveFromDownloads)
{
    const QString filename = QFileInfo(path).fileName();

    for (auto& mod : mods) {
        if (sameName(filename, mod.name)) {
            // if the mod is not yet matched and doesn't have a hash then
            // just match it with the file that has the exact same name
            if (!mod.matched && mod.hash.isEmpty()) {
                markFound(mod, path, downloadDir, moveFromDownloads);
                return false;
            }
            return true;
        }
        if (laxSameName(filename, mod.name)) {
            return true;
        }
    }

    return false;
}

bool matchByHash(QList<BlockedMod>& mods, const QString& hash, const QString& path, const QString& downloadDir, bool moveFromDownloads)
{
    for (auto& mod : mods) {
        if (mod.matched) {
            continue;
        }
        if (mod.hash.compare(hash, Qt::CaseInsensitive) == 0) {
            markFound(mod, path, downloadDir, moveFromDownloads);
            return true;
        }
    }

    return false;
}

bool allFound(const QList<BlockedMod>& mods)
{
    return std::ranges::all_of(mods, [](const auto& mod) { return mod.matched; });
}

}  // namespace BlockedModsMatcher
