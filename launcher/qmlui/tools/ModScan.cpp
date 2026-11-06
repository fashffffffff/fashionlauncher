// SPDX-License-Identifier: GPL-3.0-only
#include "ModScan.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>

#include <toml++/toml.h>

#include "ZipPeek.h"

namespace ModScan {
namespace {

// ---- metadata parsing ----------------------------------------------------------------------

QJsonObject parseJsonLenient(QByteArray data)
{
    // fabric.mod.json files in the wild contain raw newlines/tabs inside strings, which Fabric accepts
    for (char& c : data) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
    }
    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(data, &err);
    return err.error == QJsonParseError::NoError ? doc.object() : QJsonObject();
}

Descriptor readFabric(const QByteArray& data)
{
    Descriptor d;
    d.present = true;
    const QJsonObject o = parseJsonLenient(data);
    const QString id = o.value("id").toString();
    if (id.isEmpty()) {
        return d;
    }
    d.parsed = true;
    d.ids << id;
    d.name = o.value("name").toString(id);
    d.version = o.value("version").toString();
    for (const auto& p : o.value("provides").toArray()) {
        if (p.isString()) {
            d.provides.insert(p.toString());
        }
    }
    const QJsonObject depends = o.value("depends").toObject();
    for (auto it = depends.begin(); it != depends.end(); ++it) {
        if (it.key() == "minecraft") {
            d.mcPredicate = it.value();
        }
        d.depends << it.key();
    }
    return d;
}

Descriptor readQuilt(const QByteArray& data)
{
    Descriptor d;
    d.present = true;
    const QJsonObject loader = parseJsonLenient(data).value("quilt_loader").toObject();
    const QString id = loader.value("id").toString();
    if (id.isEmpty()) {
        return d;
    }
    d.parsed = true;
    d.ids << id;
    d.name = loader.value("metadata").toObject().value("name").toString(id);
    d.version = loader.value("version").toString();
    for (const auto& p : loader.value("provides").toArray()) {
        const QString pid = p.isString() ? p.toString() : p.toObject().value("id").toString();
        if (!pid.isEmpty()) {
            d.provides.insert(pid);
        }
    }
    for (const auto& dep : loader.value("depends").toArray()) {
        if (dep.isString()) {
            d.depends << dep.toString();
            continue;
        }
        const QJsonObject obj = dep.toObject();
        // optional ones and ones with an "unless" escape hatch aren't certain requirements
        if (obj.value("optional").toBool(false) || obj.contains("unless") || obj.value("id").toString().isEmpty()) {
            continue;
        }
        const QString depId = obj.value("id").toString();
        if (depId == "minecraft") {
            const QJsonValue v = obj.value("versions");
            if (v.isString() || v.isArray()) {
                d.mcPredicate = v;
            }
        }
        d.depends << depId;
    }
    return d;
}

QString tomlString(toml::node_view<toml::node> n)
{
    if (auto s = n.as_string()) {
        return QString::fromStdString(s->get());
    }
    return {};
}

Descriptor readModsToml(const QByteArray& data)
{
    Descriptor d;
    d.present = true;
    toml::table table;
#if TOML_EXCEPTIONS
    try {
        table = toml::parse(data.toStdString());
    } catch ([[maybe_unused]] const std::exception& err) {
        return d;
    }
#else
    toml::parse_result result = toml::parse(data.toStdString());
    if (!result) {
        return d;
    }
    table = result.table();
#endif
    auto* mods = table["mods"].as_array();
    if (!mods) {
        return d;
    }
    for (auto& m : *mods) {
        auto* t = m.as_table();
        if (!t) {
            continue;
        }
        const QString id = tomlString((*t)["modId"]);
        if (id.isEmpty()) {
            continue;
        }
        d.ids << id;
        if (d.name.isEmpty()) {
            d.name = tomlString((*t)["displayName"]);
            d.version = tomlString((*t)["version"]);
        }
        // [[mods]] provides = ["..."] (NeoForge)
        if (auto* prov = (*t)["provides"].as_array()) {
            for (auto& p : *prov) {
                if (auto s = p.as_string()) {
                    d.provides.insert(QString::fromStdString(s->get()));
                }
            }
        }
    }
    if (d.ids.isEmpty()) {
        return d;
    }
    d.parsed = true;
    if (d.name.isEmpty()) {
        d.name = d.ids.first();
    }
    if (d.version.contains("${")) {
        d.version.clear();
    }
    auto readDeps = [&d](toml::array* arr) {
        if (!arr) {
            return;
        }
        for (auto& dep : *arr) {
            auto* t = dep.as_table();
            if (!t) {
                continue;
            }
            const QString modId = tomlString((*t)["modId"]);
            if (modId.isEmpty()) {
                continue;
            }
            bool required = false;
            if (auto type = (*t)["type"].as_string()) {
                required = type->get() == "required";
            } else if (auto mandatory = (*t)["mandatory"].as_boolean()) {
                required = mandatory->get();
            }
            if (!required) {
                continue;
            }
            const QString side = tomlString((*t)["side"]).toUpper();
            if (side == "SERVER") {
                continue;  // not needed to play
            }
            if (modId == "minecraft") {
                d.mcRange = tomlString((*t)["versionRange"]);
            }
            d.depends << modId;
        }
    };
    auto depValue = table["dependencies"];
    if (auto* arr = depValue.as_array()) {
        readDeps(arr);
    } else if (auto* deps = depValue.as_table()) {
        for (auto& [key, value] : *deps) {
            readDeps(value.as_array());
        }
    }
    return d;
}

// ---- jar reading ---------------------------------------------------------------------------

void scanZip(ZipPeek& zip, Jar& jar, int depth)
{
    jar.readable = true;
    if (zip.contains("fabric.mod.json")) {
        jar.fabric = readFabric(zip.read("fabric.mod.json", 1024 * 1024));
    }
    if (zip.contains("quilt.mod.json")) {
        jar.quilt = readQuilt(zip.read("quilt.mod.json", 1024 * 1024));
    }
    if (zip.contains("META-INF/mods.toml")) {
        jar.forge = readModsToml(zip.read("META-INF/mods.toml", 1024 * 1024));
    }
    if (zip.contains("META-INF/neoforge.mods.toml")) {
        jar.neoforge = readModsToml(zip.read("META-INF/neoforge.mods.toml", 1024 * 1024));
    }
    if (depth >= 3) {
        return;
    }
    for (const QString& name : zip.names()) {
        if (!name.endsWith(".jar", Qt::CaseInsensitive) ||
            !(name.startsWith("META-INF/jars/") || name.startsWith("META-INF/jarjar/") || name.startsWith("META-INF/jarsinjar/"))) {
            continue;
        }
        QByteArray inner = zip.read(name, 64 * 1024 * 1024);
        if (inner.isEmpty()) {
            continue;
        }
        QBuffer buffer(&inner);
        buffer.open(QIODevice::ReadOnly);
        ZipPeek innerZip;
        if (!innerZip.open(&buffer)) {
            continue;
        }
        Jar nested;
        scanZip(innerZip, nested, depth + 1);
        for (const Descriptor* d : { &nested.fabric, &nested.quilt, &nested.forge, &nested.neoforge }) {
            for (const QString& id : d->ids) {
                jar.nestedIds.insert(id);
            }
            jar.nestedIds.unite(d->provides);
        }
        jar.nestedIds.unite(nested.nestedIds);
    }
}

struct CacheEntry {
    qint64 size;
    qint64 mtime;
    Jar jar;
};
QMutex g_cacheLock;
QHash<QString, CacheEntry> g_cache;

// ---- versions ------------------------------------------------------------------------------

using Parts = QList<int>;

bool parseParts(const QString& s, Parts& out)
{
    static const QRegularExpression re(R"(^\d+(\.\d+)*$)");
    if (!re.match(s).hasMatch()) {
        return false;
    }
    out.clear();
    for (const auto& p : s.split('.')) {
        out << p.toInt();
    }
    return true;
}

int cmp(const Parts& a, const Parts& b)
{
    const auto n = qMax(a.size(), b.size());
    for (qsizetype i = 0; i < n; ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

QString joinParts(const Parts& p)
{
    QStringList s;
    for (int v : p) {
        s << QString::number(v);
    }
    return s.join('.');
}

/// One term of a Fabric predicate turned into lower/upper bounds; false when unsupported.
struct Bounds {
    bool hasLow = false, lowIncl = true, hasHigh = false, highIncl = false;
    Parts low, high;
    bool any = false;
};

bool termBounds(QString term, Bounds& b)
{
    term = term.trimmed();
    if (term.isEmpty() || term == "*" || term == "x" || term == "X") {
        b.any = true;
        return true;
    }
    QString op;
    for (const char* candidate : { ">=", "<=", ">", "<", "=", "~", "^" }) {
        if (term.startsWith(QLatin1String(candidate))) {
            op = QLatin1String(candidate);
            break;
        }
    }
    QString v = term.mid(op.size()).trimmed();
    const int plus = v.indexOf('+');
    if (plus >= 0) {
        v.truncate(plus);
    }
    bool suffix = false;
    const int dash = v.indexOf('-');
    if (dash >= 0) {
        suffix = true;
        v.truncate(dash);
    }
    bool wildcard = false;
    static const QRegularExpression wild(R"(\.[xX*]$)");
    while (wild.match(v).hasMatch()) {
        wildcard = true;
        v.chop(2);
    }
    Parts p;
    if (!parseParts(v, p)) {
        return false;
    }
    if (wildcard) {
        if (!op.isEmpty() && op != "=") {
            return false;
        }
        Parts next = p;
        next.last() += 1;
        b.hasLow = b.hasHigh = true;
        b.low = p;
        b.high = next;
        return true;
    }
    if (op.isEmpty() || op == "=") {
        if (suffix) {
            return false;  // a pre-release: never a release, but don't guess
        }
        b.hasLow = b.hasHigh = true;
        b.lowIncl = b.highIncl = true;
        b.low = b.high = p;
        return true;
    }
    if (op == ">=" || op == ">") {
        b.hasLow = true;
        b.low = p;
        b.lowIncl = op == ">=" || suffix;
        return true;
    }
    if (op == "<" || op == "<=") {
        b.hasHigh = true;
        b.high = p;
        b.highIncl = op == "<=" && !suffix;
        return true;
    }
    if (op == "~" || op == "^") {
        b.hasLow = b.hasHigh = true;
        b.low = p;
        Parts next;
        if (op == "^" || p.size() == 1) {
            next = { p[0] + 1 };
        } else {
            next = { p[0], p[1] + 1 };
        }
        b.high = next;
        return true;
    }
    return false;
}

int matchBounds(const Bounds& b, const Parts& v)
{
    if (b.any) {
        return 1;
    }
    if (b.hasLow) {
        const int c = cmp(v, b.low);
        if (c < 0 || (c == 0 && !b.lowIncl)) {
            return 0;
        }
    }
    if (b.hasHigh) {
        const int c = cmp(v, b.high);
        if (c > 0 || (c == 0 && !b.highIncl)) {
            return 0;
        }
    }
    return 1;
}

int matchFabricString(const QString& predicate, const Parts& v)
{
    int result = 1;
    for (const auto& term : predicate.split(' ', Qt::SkipEmptyParts)) {
        Bounds b;
        if (!termBounds(term, b)) {
            result = -1;
            continue;
        }
        if (matchBounds(b, v) == 0) {
            return 0;
        }
    }
    return result;
}

QString describeBounds(const Bounds& b)
{
    if (b.any) {
        return QObject::tr("any version");
    }
    if (b.hasLow && b.hasHigh) {
        if (b.lowIncl && b.highIncl && cmp(b.low, b.high) == 0) {
            return joinParts(b.low);
        }
        // >=1.20 <1.21 and friends read as "1.20.x"
        if (b.lowIncl && !b.highIncl && b.high.size() >= 2) {
            const auto n = b.high.size();
            Parts family = b.low.mid(0, n);
            while (family.size() < n) {
                family << 0;
            }
            Parts next = family;
            next.last() += 1;
            if (cmp(next, b.high) == 0) {
                bool lowIsStart = true;
                for (qsizetype i = n; i < b.low.size(); ++i) {
                    lowIsStart = lowIsStart && b.low[i] == 0;
                }
                return lowIsStart ? joinParts(family) + ".x" : QObject::tr("%1 to %2").arg(joinParts(b.low), joinParts(family) + ".x");
            }
        }
        return b.highIncl ? QObject::tr("%1 to %2").arg(joinParts(b.low), joinParts(b.high))
                          : QObject::tr("%1 up to (not including) %2").arg(joinParts(b.low), joinParts(b.high));
    }
    if (b.hasLow) {
        return b.lowIncl ? QObject::tr("%1 or newer").arg(joinParts(b.low)) : QObject::tr("newer than %1").arg(joinParts(b.low));
    }
    if (b.hasHigh) {
        return b.highIncl ? QObject::tr("%1 or older").arg(joinParts(b.high)) : QObject::tr("older than %1").arg(joinParts(b.high));
    }
    return {};
}

/// Merges the terms of one AND-predicate into a single range (for wording only).
Bounds mergeTerms(const QString& predicate)
{
    Bounds out;
    out.any = true;
    for (const auto& term : predicate.split(' ', Qt::SkipEmptyParts)) {
        Bounds b;
        if (!termBounds(term, b) || b.any) {
            continue;
        }
        out.any = false;
        if (b.hasLow && (!out.hasLow || cmp(b.low, out.low) > 0)) {
            out.hasLow = true;
            out.low = b.low;
            out.lowIncl = b.lowIncl;
        }
        if (b.hasHigh && (!out.hasHigh || cmp(b.high, out.high) < 0)) {
            out.hasHigh = true;
            out.high = b.high;
            out.highIncl = b.highIncl;
        }
    }
    return out;
}

bool mavenBounds(const QString& set, Bounds& b)
{
    // set like "[1.20,1.21)" or "[1.20.1]"
    if (set.size() < 2) {
        return false;
    }
    const QChar open = set.front();
    const QChar close = set.back();
    const QString inner = set.mid(1, set.size() - 2);
    const int comma = inner.indexOf(',');
    if (comma < 0) {
        Parts p;
        if (open != '[' || close != ']' || !parseParts(inner.trimmed(), p)) {
            return false;
        }
        b.hasLow = b.hasHigh = true;
        b.lowIncl = b.highIncl = true;
        b.low = b.high = p;
        return true;
    }
    const QString lo = inner.left(comma).trimmed();
    const QString hi = inner.mid(comma + 1).trimmed();
    if (!lo.isEmpty()) {
        if (!parseParts(lo, b.low)) {
            return false;
        }
        b.hasLow = true;
        b.lowIncl = open == '[';
    }
    if (!hi.isEmpty()) {
        if (!parseParts(hi, b.high)) {
            return false;
        }
        b.hasHigh = true;
        b.highIncl = close == ']';
    }
    b.any = !b.hasLow && !b.hasHigh;
    return true;
}

QStringList mavenSets(const QString& range)
{
    static const QRegularExpression re(R"([\[\(][^\]\)]*[\]\)])");
    QStringList sets;
    auto it = re.globalMatch(range);
    while (it.hasNext()) {
        sets << it.next().captured(0);
    }
    return sets;
}

}  // namespace

Jar scan(const QString& path)
{
    const QFileInfo info(path);
    const qint64 size = info.size();
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    {
        QMutexLocker lock(&g_cacheLock);
        auto it = g_cache.constFind(path);
        if (it != g_cache.constEnd() && it->size == size && it->mtime == mtime) {
            return it->jar;
        }
    }
    Jar jar;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        ZipPeek zip;
        if (zip.open(&file)) {
            scanZip(zip, jar, 0);
        }
    }
    QMutexLocker lock(&g_cacheLock);
    g_cache.insert(path, { size, mtime, jar });
    return jar;
}

bool isRelease(const QString& version)
{
    static const QRegularExpression re(R"(^\d+\.\d+(\.\d+)?$)");
    return re.match(version).hasMatch();
}

int matchFabric(const QJsonValue& predicate, const QString& version)
{
    Parts v;
    if (!isRelease(version) || !parseParts(version, v)) {
        return -1;
    }
    if (predicate.isString()) {
        return matchFabricString(predicate.toString(), v);
    }
    if (predicate.isArray()) {
        const auto arr = predicate.toArray();
        if (arr.isEmpty()) {
            return -1;
        }
        int result = 0;
        for (const auto& p : arr) {
            if (!p.isString()) {
                return -1;
            }
            const int r = matchFabricString(p.toString(), v);
            if (r == 1) {
                return 1;
            }
            if (r == -1) {
                result = -1;
            }
        }
        return result;
    }
    return -1;
}

int matchMaven(const QString& range, const QString& version)
{
    Parts v;
    if (!isRelease(version) || !parseParts(version, v)) {
        return -1;
    }
    const QStringList sets = mavenSets(range.trimmed());
    if (sets.isEmpty()) {
        return -1;  // bare "1.20.1" is a soft requirement in Maven terms
    }
    int result = 0;
    for (const auto& s : sets) {
        Bounds b;
        if (!mavenBounds(s, b)) {
            result = -1;
            continue;
        }
        if (matchBounds(b, v) == 1) {
            return 1;
        }
    }
    return result;
}

QString describeFabric(const QJsonValue& predicate)
{
    QStringList parts;
    const QJsonArray arr = predicate.isArray() ? predicate.toArray() : QJsonArray{ predicate };
    for (const auto& p : arr) {
        const QString d = describeBounds(mergeTerms(p.toString()));
        if (!d.isEmpty() && !parts.contains(d)) {
            parts << d;
        }
    }
    return parts.join(QObject::tr(" or "));
}

QString describeMaven(const QString& range)
{
    QStringList parts;
    for (const auto& s : mavenSets(range)) {
        Bounds b;
        if (mavenBounds(s, b)) {
            const QString d = describeBounds(b);
            if (!d.isEmpty() && !parts.contains(d)) {
                parts << d;
            }
        }
    }
    return parts.join(QObject::tr(" or "));
}

}  // namespace ModScan
