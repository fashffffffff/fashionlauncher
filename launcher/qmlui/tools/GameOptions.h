// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>

/**
 * Copies Minecraft's own game settings (options.txt, plus optionsof.txt, optionsshaders.txt and
 * config/sodium-options.json when present) from one instance to others. Exposed to QML as "GameOptions".
 *
 * Keys are grouped into categories the player recognises: controls, video, sound, chat (chat and
 * accessibility), language (language and skin parts) and other. Only the chosen categories are copied; every
 * other key of the target stays as it was, and the target's `version:` is never touched.
 *
 * Versions: key bindings changed format in 1.13 (LWJGL 2 numbers → "key.keyboard.w"). Between an existing
 * target file and a source of the other format they are converted; a key without an equivalent is skipped
 * rather than written in the wrong format. A target without options.txt gets the source's lines and its
 * `version:` so Minecraft's own data fixers upgrade them. Language codes (lower case since 1.11) and
 * fancyGraphics/graphicsMode (1.16) are converted as well.
 *
 * Every file is backed up next to itself as `<name>.bak-<yyyyMMdd-HHmmss>` (three kept per file) before it is
 * written; undo() puts back the files of the last transfer.
 */
class GameOptions : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(int undoCount READ undoCount NOTIFY undoChanged)

   public:
    explicit GameOptions(QObject* parent = nullptr);

    bool canUndo() const { return !m_last.isEmpty(); }
    int undoCount() const { return m_lastTargets; }

    /// What the instance's game settings hold: { exists, gameVersion, categories: { <cat>: { count, ... } } }.
    Q_INVOKABLE QVariantMap summary(const QString& id) const;
    /// Dry run of apply(): { targets, changed, converted, skipped, running: [names], empty: [names] }.
    Q_INVOKABLE QVariantMap plan(const QString& sourceId, const QStringList& targetIds, const QStringList& categories) const;
    /// Copies the chosen categories. Same result as plan() plus { ok, error, written }.
    Q_INVOKABLE QVariantMap apply(const QString& sourceId, const QStringList& targetIds, const QStringList& categories);
    /// Puts back every file the last apply() wrote. Returns false when something couldn't be restored.
    Q_INVOKABLE bool undo();

    /// Ids of all instances except `exceptId`, most recently played first.
    Q_INVOKABLE QStringList allOtherInstances(const QString& exceptId) const;

   signals:
    void undoChanged();

   private:
    struct Written {
        QString path;
        QString backup;  ///< empty when the file didn't exist before
    };
    /// `written` == nullptr is a dry run.
    static QVariantMap run(const QString& sourceId, const QStringList& targetIds, const QStringList& categories, QList<Written>* written);

    QList<Written> m_last;
    int m_lastTargets = 0;
};
