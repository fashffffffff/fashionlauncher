// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QVariantList>

#include "tasks/ConcurrentTask.h"
#include "ui/dialogs/BlockedModsDialog.h"

class QEventLoop;

/**
 * The QML side of the "blocked mods" question (`BlockedMods` in QML): some CurseForge mods
 * forbid third-party launchers from downloading them, so a pack install stops and asks the
 * user to fetch them by hand. FlameInstanceCreationTask calls askForMods() and waits in its
 * own thread (like it waits on the classic dialog); this object emits `requested`, watches the
 * downloads folder plus any folder the user adds, recognises files by hash — not just by name —
 * as they appear, and QML answers with confirm() or cancel().
 *
 * The model exposed to QML is a list of { name, websiteUrl, hash, size, found, localPath,
 * disabled }; `size` stays 0 (unknown) because Prism's CurseForge data does not carry it.
 */
class BlockedModsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList mods READ mods NOTIFY modsChanged)
    Q_PROPERTY(bool active READ active NOTIFY modsChanged)
    Q_PROPERTY(bool allFound READ allFound NOTIFY modsChanged)
    Q_PROPERTY(int foundCount READ foundCount NOTIFY modsChanged)
    Q_PROPERTY(QStringList watchedDirs READ watchedDirs NOTIFY modsChanged)

   public:
    explicit BlockedModsBridge(QObject* parent = nullptr);
    /// The shell going away (game launch, quit) counts as cancelling an open question.
    ~BlockedModsBridge() override;

    /// Show the question in the QML window and wait for the answer. On Continue, `mods` comes
    /// back with the matched files filled in; false means the user cancelled.
    bool askForMods(QList<BlockedMod>& mods);

    /// Debug: three fake entries to preview the dialog (FFPRISM_PAGE=blocked).
    Q_INVOKABLE void preview();

    QVariantList mods() const;
    bool active() const { return m_active; }
    bool allFound() const;
    int foundCount() const;
    QStringList watchedDirs() const { return m_watcher.directories(); }

    Q_INVOKABLE void openPage(int index);
    Q_INVOKABLE void openAll();
    Q_INVOKABLE void addFolder(const QUrl& url);
    /// Hash the file the user picked (or dropped, index -1) and match it against every entry.
    Q_INVOKABLE void pickFile(int index, const QUrl& url);
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel();

   signals:
    /// The dialog should open and the window come to the front.
    void requested();
    /// The question is over (either way): close the dialog.
    void finished();
    void modsChanged();

   private:
    QString downloadsDir() const;
    bool moveFromDownloads() const;
    void setupWatch();
    void watchPath(const QString& path, bool watchRecursive = false);
    void scanPaths();
    void scanPath(const QString& path);
    void queueHashTask(const QString& path);
    void runHashTasks();
    void hashTasksFinished();
    void validateMatchedMods();
    void stopWatching();

    QList<BlockedMod> m_mods;
    bool m_active = false;
    bool m_accepted = false;
    QPointer<QEventLoop> m_loop;
    QFileSystemWatcher m_watcher;
    shared_qobject_ptr<ConcurrentTask> m_hashingTask;
    QSet<QString> m_pendingHashPaths;
    bool m_rehashPending = false;
};
