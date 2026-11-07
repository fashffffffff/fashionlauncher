#include "PlayStats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>

#include "Application.h"
#include "BaseInstance.h"
#include "GZip.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

namespace {

constexpr qint64 kMinSession = 30;         // shorter runs are crashes / quick checks
constexpr qint64 kMaxSession = 24 * 3600;  // a log can't honestly cover more

struct Meta {
    QString version;
    QString loader;
};

Meta metaOf(BaseInstance* base)
{
    Meta m;
    auto* inst = dynamic_cast<MinecraftInstance*>(base);
    if (!inst) {
        return m;
    }
    auto* profile = inst->getPackProfile();
    m.version = profile->getComponentVersion("net.minecraft");
    if (m.version.isEmpty()) {
        if (auto res = profile->reload(Net::Mode::Offline); !res) {
            return m;
        }
        m.version = profile->getComponentVersion("net.minecraft");
    }
    static const QList<QPair<QString, QString>> loaders = {
        { "net.neoforged", "NeoForge" },           { "net.minecraftforge", "Forge" },
        { "net.fabricmc.fabric-loader", "Fabric" }, { "org.quiltmc.quilt-loader", "Quilt" },
        { "com.mumfrey.liteloader", "LiteLoader" },
    };
    for (const auto& [uid, label] : loaders) {
        if (!profile->getComponentVersion(uid).isEmpty()) {
            m.loader = label;
            break;
        }
    }
    return m;
}

struct Session {
    qint64 start = 0;  // ms since epoch
    qint64 secs = 0;
    QString instance, name, mc, loader;
    bool backfill = false;
};

QList<Session> readLog(const QString& path)
{
    QList<Session> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    while (!f.atEnd()) {
        const auto line = f.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto o = QJsonDocument::fromJson(line).object();
        Session s;
        s.start = o.value("start").toInteger();
        s.secs = o.value("secs").toInteger();
        if (s.start <= 0 || s.secs <= 0) {
            continue;
        }
        s.instance = o.value("instance").toString();
        s.name = o.value("name").toString();
        s.mc = o.value("mc").toString();
        s.loader = o.value("loader").toString();
        s.backfill = o.value("backfill").toBool();
        out.append(s);
    }
    return out;
}

// ---- backfill from game logs ----------------------------------------------------------------------------------

struct InstInfo {
    QString id, name, root;
    Meta meta;
};

// "[14:22:01]" (vanilla/Fabric) or "[05Nov2026 14:22:01.123]" (Forge). Returns seconds of the day, -1 if none.
const QRegularExpression& stampRe()
{
    static const QRegularExpression re(R"(^\[(?:\d{2}[A-Za-z]{3}\d{4} )?(\d{2}):(\d{2}):(\d{2}))");
    return re;
}

int stampOf(const QByteArray& line)
{
    if (line.isEmpty() || line[0] != '[') {
        return -1;
    }
    const auto m = stampRe().match(QString::fromLatin1(line.left(32)));
    if (!m.hasMatch()) {
        return -1;
    }
    return m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toInt();
}

// First and last timestamp in a log; plain text or gzip.
QPair<int, int> stampsOf(const QString& path, bool gz)
{
    int first = -1, last = -1;
    QByteArray tail;
    auto eat = [&](const QByteArray& block) {
        QByteArray data = tail + block;
        int from = 0;
        for (int nl = data.indexOf('\n'); nl >= 0; nl = data.indexOf('\n', from)) {
            const int t = stampOf(data.mid(from, nl - from));
            if (t >= 0) {
                if (first < 0) {
                    first = t;
                }
                last = t;
            }
            from = nl + 1;
        }
        tail = data.mid(from);
        return true;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return { -1, -1 };
    }
    if (gz) {
        GZip::readGzFileByBlocks(&f, eat);
    } else {
        while (!f.atEnd()) {
            eat(f.read(1 << 16));
        }
    }
    eat("\n");
    return { first, last };
}

QList<QVariantMap> scanInstances(const QList<InstInfo>& insts)
{
    static const QRegularExpression dated(R"(^(\d{4}-\d{2}-\d{2})-\d+\.log\.gz$)");
    QList<QVariantMap> out;
    for (const auto& inst : insts) {
        const QDir logs(QDir(inst.root).filePath("logs"));
        const auto files = logs.entryInfoList({ "*.log.gz", "latest.log" }, QDir::Files);
        for (const auto& fi : files) {
            const bool gz = fi.fileName().endsWith(".gz");
            QDate day;
            if (gz) {
                const auto m = dated.match(fi.fileName());
                if (!m.hasMatch()) {
                    continue;  // debug-*.log.gz duplicates the main log
                }
                day = QDate::fromString(m.captured(1), Qt::ISODate);
            }
            const auto [first, last] = stampsOf(fi.absoluteFilePath(), gz);
            if (first < 0) {
                continue;
            }
            qint64 secs = last - first;
            if (secs < 0) {
                secs += 24 * 3600;  // crossed midnight
            }
            if (secs < kMinSession || secs > kMaxSession) {
                continue;
            }
            qint64 start;
            if (gz) {
                start = QDateTime(day, QTime(0, 0).addSecs(first)).toMSecsSinceEpoch();
            } else {
                // latest.log: the file's mtime is the end of the session.
                start = fi.lastModified().toMSecsSinceEpoch() - secs * 1000;
            }
            out.append({ { "start", start },
                         { "secs", secs },
                         { "instance", inst.id },
                         { "name", inst.name },
                         { "mc", inst.meta.version },
                         { "loader", inst.meta.loader },
                         { "backfill", true } });
        }
    }
    std::sort(out.begin(), out.end(),
              [](const QVariantMap& a, const QVariantMap& b) { return a["start"].toLongLong() < b["start"].toLongLong(); });
    return out;
}

// Splits a session over the local days it covers.
void addToDays(QHash<QDate, qint64>& days, const Session& s)
{
    QDateTime at = QDateTime::fromMSecsSinceEpoch(s.start);
    qint64 left = s.secs;
    while (left > 0) {
        const QDateTime nextMidnight(at.date().addDays(1), QTime(0, 0));
        const qint64 chunk = std::min(left, std::max<qint64>(1, at.secsTo(nextMidnight)));
        days[at.date()] += chunk;
        left -= chunk;
        at = nextMidnight;
    }
}

QVariantList topOf(const QHash<QString, qint64>& m, int n)
{
    QList<QPair<QString, qint64>> v;
    for (auto it = m.begin(); it != m.end(); ++it) {
        if (it.value() > 0) {
            v.append({ it.key(), it.value() });
        }
    }
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QVariantList out;
    for (int i = 0; i < v.size() && i < n; ++i) {
        out.append(QVariantMap{ { "label", v[i].first }, { "secs", v[i].second } });
    }
    return out;
}

}  // namespace

PlayStats::PlayStats(QObject* parent) : QObject(parent) {}

PlayStats* PlayStats::instance()
{
    static auto* self = new PlayStats();
    return self;
}

QString PlayStats::dir()
{
    return QDir(APPLICATION->dataRoot()).filePath("stats");
}

QString PlayStats::logPath()
{
    return QDir(dir()).filePath("sessions.jsonl");
}

void PlayStats::append(const QList<QVariantMap>& entries)
{
    if (entries.isEmpty()) {
        return;
    }
    QDir().mkpath(dir());
    QFile f(logPath());
    if (!f.open(QIODevice::Append | QIODevice::WriteOnly)) {
        qWarning() << "PlayStats: can't write" << f.fileName();
        return;
    }
    for (const auto& e : entries) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(e)).toJson(QJsonDocument::Compact) + '\n');
    }
    f.close();
    ++m_revision;
    emit changed();
}

void PlayStats::record(BaseInstance* inst, const QDateTime& start, qint64 seconds)
{
    if (!inst || seconds < kMinSession) {
        return;
    }
    const auto meta = metaOf(inst);
    instance()->append({ QVariantMap{ { "start", start.toMSecsSinceEpoch() },
                                      { "secs", seconds },
                                      { "instance", inst->id() },
                                      { "name", inst->name() },
                                      { "mc", meta.version },
                                      { "loader", meta.loader } } });
}

void PlayStats::ensureBackfill()
{
    const QString marker = QDir(dir()).filePath("backfill.done");
    if (m_backfilling || QFile::exists(marker)) {
        return;
    }
    QList<InstInfo> insts;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        auto* inst = list->at(i);
        insts.append({ inst->id(), inst->name(), inst->gameRoot(), metaOf(inst) });
    }
    m_backfilling = true;
    emit changed();
    auto* watcher = new QFutureWatcher<QList<QVariantMap>>(this);
    connect(watcher, &QFutureWatcher<QList<QVariantMap>>::finished, this, [this, watcher, marker] {
        const auto entries = watcher->result();
        watcher->deleteLater();
        // Sessions recorded live after the log was rotated would be counted twice; the logs only cover the past.
        const auto existing = readLog(logPath());
        QList<QVariantMap> fresh;
        for (const auto& e : entries) {
            const qint64 s = e["start"].toLongLong();
            const bool dup = std::any_of(existing.begin(), existing.end(), [&](const Session& x) {
                return x.instance == e["instance"].toString() && std::abs(x.start - s) < 120 * 1000;
            });
            if (!dup) {
                fresh.append(e);
            }
        }
        append(fresh);
        QDir().mkpath(dir());
        QFile m(marker);
        if (m.open(QIODevice::WriteOnly)) {
            m.write(QString::number(fresh.size()).toUtf8());
        }
        m_backfilling = false;
        ++m_revision;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([insts] { return scanInstances(insts); }));
}

QVariantMap PlayStats::summary(int firstDayOfWeek) const
{
    const auto sessions = readLog(logPath());
    const QDate today = QDate::currentDate();

    QHash<QDate, qint64> days;
    QHash<QString, qint64> byVersion, byLoader, loggedByInstance;
    qint64 logged = 0;
    int backfilled = 0;
    for (const auto& s : sessions) {
        addToDays(days, s);
        logged += s.secs;
        loggedByInstance[s.instance] += s.secs;
        if (!s.mc.isEmpty()) {
            byVersion[s.mc] += s.secs;
        }
        byLoader[s.loader] += s.secs;
        backfilled += s.backfill ? 1 : 0;
    }

    // Instances: Prism's own counters are authoritative for totals. Time they know about but the log doesn't
    // (played before this log existed and the logs were deleted) is attributed to the instance's current version.
    QVariantList top;
    qint64 counted = 0;
    {
        struct Row {
            QString id, name, icon;
            qint64 secs;
        };
        QList<Row> rows;
        auto* list = APPLICATION->instances();
        for (int i = 0; i < list->count(); ++i) {
            auto* inst = list->at(i);
            const qint64 total = std::max<qint64>(inst->totalTimePlayed(), loggedByInstance.value(inst->id()));
            counted += total;
            const qint64 extra = total - loggedByInstance.value(inst->id());
            if (extra > 0) {
                const auto meta = metaOf(inst);
                if (!meta.version.isEmpty()) {
                    byVersion[meta.version] += extra;
                }
                byLoader[meta.loader] += extra;
            }
            if (total > 0) {
                rows.append({ inst->id(), inst->name(), QString("image://instanceicon/%1").arg(inst->iconKey()), total });
            }
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.secs > b.secs; });
        for (int i = 0; i < rows.size() && i < 5; ++i) {
            top.append(QVariantMap{ { "id", rows[i].id }, { "name", rows[i].name }, { "icon", rows[i].icon }, { "secs", rows[i].secs } });
        }
    }

    // Heatmap: 53 week columns ending with the current week; weeks start on the locale's first day.
    const int firstDow = firstDayOfWeek <= 0 || firstDayOfWeek > 7 ? 7 : firstDayOfWeek;
    const int offset = (today.dayOfWeek() - firstDow + 7) % 7;
    const QDate gridStart = today.addDays(-offset - 52 * 7);
    QVariantList cells;
    qint64 maxDay = 0;
    for (QDate d = gridStart; d <= today; d = d.addDays(1)) {
        maxDay = std::max(maxDay, days.value(d));
    }
    for (QDate d = gridStart; d <= today; d = d.addDays(1)) {
        const qint64 s = days.value(d);
        const int level = s <= 0 ? 0 : s < 30 * 60 ? 1 : s < 3600 ? 2 : s < 2 * 3600 ? 3 : 4;
        cells.append(QVariantMap{ { "date", d.toString(Qt::ISODate) }, { "secs", s }, { "level", level } });
    }

    // Streaks.
    int longest = 0, run = 0;
    if (!days.isEmpty()) {
        auto keys = days.keys();
        std::sort(keys.begin(), keys.end());
        QDate prev;
        for (const auto& d : keys) {
            if (days.value(d) <= 0) {
                continue;
            }
            run = (prev.isValid() && prev.addDays(1) == d) ? run + 1 : 1;
            longest = std::max(longest, run);
            prev = d;
        }
    }
    int current = 0;
    for (QDate d = days.value(today) > 0 ? today : today.addDays(-1); days.value(d) > 0; d = d.addDays(-1)) {
        ++current;
    }

    qint64 week = 0, month = 0;
    const QDate weekStart = today.addDays(-offset);
    for (auto it = days.begin(); it != days.end(); ++it) {
        if (it.key() >= weekStart && it.key() <= today) {
            week += it.value();
        }
        if (it.key().year() == today.year() && it.key().month() == today.month()) {
            month += it.value();
        }
    }

    return {
        { "cells", cells },
        { "firstDay", gridStart.toString(Qt::ISODate) },
        { "maxDay", maxDay },
        { "week", week },
        { "month", month },
        { "allTime", std::max(logged, counted) },
        { "sessions", sessions.size() },
        { "backfilled", backfilled },
        { "avgSession", sessions.isEmpty() ? 0 : logged / sessions.size() },
        { "currentStreak", current },
        { "longestStreak", longest },
        { "topInstances", top },
        { "versions", topOf(byVersion, 5) },
        { "loaders", topOf(byLoader, 5) },
    };
}
