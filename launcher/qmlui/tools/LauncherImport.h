// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QVariantList>
#include <atomic>
#include <memory>

#include "InstanceTask.h"

/**
 * Finding and importing instances of other launchers: CurseForge app, Modrinth App, ATLauncher,
 * MultiMC/PolyMC/Prism and the official Minecraft launcher, at their default places on
 * macOS, Windows and Linux.
 *
 * Each found instance is a map:
 *   key, launcher (display name), name, path (instance folder), gameDir (folder with mods/saves),
 *   mc, loader ("", fabric, quilt, forge, neoforge), loaderVersion,
 *   format ("prism" = copy the whole instance folder, "folder" = build a new instance around gameDir),
 *   importable, note (why it can't be imported, or extra info)
 */
namespace LauncherImport {

/// Scans the default locations of every supported launcher.
QVariantList detect();
/// Scans a folder the user picked: an instances folder, a launcher's data folder, or one instance.
QVariantList scanFolder(const QString& path);

/// A task that creates one instance from a found entry. link = share the game folder instead of copying it.
InstanceTask* makeTask(const QVariantMap& entry, bool link);

class ForeignImportTask : public InstanceTask {
    Q_OBJECT
   public:
    ForeignImportTask(QVariantMap entry, bool link) : m_entry(std::move(entry)), m_link(link) {}
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    void finishInstance();

    QVariantMap m_entry;
    bool m_link;
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace LauncherImport
