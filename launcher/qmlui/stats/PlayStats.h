#pragma once

#include <QDateTime>
#include <QObject>
#include <QVariantMap>

class BaseInstance;

// Play statistics. Prism only keeps a total and the last session per instance, so every finished game session is
// appended to <data>/stats/sessions.jsonl (one JSON object per line). On first use the history is backfilled once
// from the instances' dated game logs, so existing users don't start with an empty screen.
// QML: context property `Stats` — `revision` changes whenever the log grows; `summary()` returns everything the
// statistics tab draws.
class PlayStats : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool backfilling READ backfilling NOTIFY changed)

   public:
    static PlayStats* instance();

    // Called by BaseInstance when the game process ends.
    static void record(BaseInstance* inst, const QDateTime& start, qint64 seconds);

    int revision() const { return m_revision; }
    bool backfilling() const { return m_backfilling; }

    // Starts the one-time backfill if it hasn't run yet.
    Q_INVOKABLE void ensureBackfill();
    // firstDayOfWeek: QML Locale value (0 = Sunday … 6), so the grid matches the UI language.
    Q_INVOKABLE QVariantMap summary(int firstDayOfWeek = 1) const;

   signals:
    void changed();

   private:
    explicit PlayStats(QObject* parent = nullptr);
    static QString dir();
    static QString logPath();
    void append(const QList<QVariantMap>& entries);

    int m_revision = 0;
    bool m_backfilling = false;
};
