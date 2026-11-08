// SPDX-License-Identifier: GPL-3.0-only
#include "GameOptions.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QDebug>

#include <algorithm>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/Mode.h"

namespace {

// ---------------------------------------------------------------- options.txt as ordered key/value lines

struct Options {
    bool exists = false;
    QChar sep = ':';  ///< optionsshaders.txt uses '='
    QStringList order;
    QHash<QString, QString> values;

    bool has(const QString& k) const { return values.contains(k); }
    QString get(const QString& k) const { return values.value(k); }
    void set(const QString& k, const QString& v)
    {
        if (!values.contains(k)) {
            order.append(k);
        }
        values.insert(k, v);
    }
    void remove(const QString& k)
    {
        values.remove(k);
        order.removeAll(k);
    }
};

Options readOptions(const QString& path, QChar sep = ':')
{
    Options o;
    o.sep = sep;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return o;
    }
    o.exists = true;
    const auto lines = QString::fromUtf8(f.readAll()).split('\n');
    for (QString line : lines) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        const int colon = line.indexOf(sep);  // values may contain ':' (Forge key modifiers), names never do
        if (colon <= 0) {
            continue;
        }
        o.set(line.left(colon), line.mid(colon + 1));
    }
    return o;
}

QByteArray serialize(const Options& o)
{
    QByteArray out;
    for (const auto& k : o.order) {
        out += (k + o.sep + o.values.value(k)).toUtf8() + '\n';
    }
    return out;
}

// ---------------------------------------------------------------- categories

const QStringList kCategories = { "controls", "video", "sound", "chat", "language", "other" };

const QSet<QString> kControls = { "mouseSensitivity", "invertYMouse", "invertXMouse", "toggleCrouch", "toggleSprint",
                                  "autoJump", "discrete_mouse_scroll", "mouseWheelSensitivity", "rawMouseInput", "touchscreen",
                                  "operatorItemsTab", "toggleAttack", "toggleUse", "sprintWindow" };
const QSet<QString> kVideo = {
    "graphicsMode", "fancyGraphics", "renderDistance", "simulationDistance", "guiScale", "fov", "maxFps", "enableVsync",
    "fullscreen", "fullscreenResolution", "gamma", "particles", "biomeBlendRadius", "entityShadows", "renderClouds", "ao",
    "bobView", "mipmapLevels", "entityDistanceScaling", "prioritizeChunkUpdates", "useVbo", "attackIndicator",
    "showAutosaveIndicator", "menuBackgroundBlurriness", "cloudRange", "inactivityFpsLimit", "overrideWidth",
    "overrideHeight", "textureFiltering", "maxAnisotropyBit", "improvedTransparency", "chunkSectionFadeInTime",
    "clouds", "anaglyph3d", "advancedOpengl", "fboEnable", "useNativeTransport", "forceUnicodeFont_unused",
    "allowBlockAlternatives", "vignette", "showFps", "heldItemTooltips", "cutoutLeaves", "vsync", "weatherRadius" };
const QSet<QString> kSound = { "soundDevice", "directionalAudio", "musicFrequency", "showNowPlayingToast", "musicToast" };
const QSet<QString> kChat = { "narrator", "showSubtitles", "highContrast", "reducedDebugInfo", "hideLightningFlashes",
                              "darkMojangStudiosBackground", "panoramaScrollSpeed", "notificationDisplayTime",
                              "onlyShowSecureChat", "hideMatchedNames", "forceUnicodeFont", "japaneseGlyphVariants",
                              "highContrastBlockOutline", "narratorHotkey", "damageTiltStrength", "textBackgroundOpacity",
                              "backgroundForChatOnly", "autoSuggestions", "screenEffectScale", "fovEffectScale",
                              "darknessEffectScale", "glintSpeed", "glintStrength", "hideSplashTexts", "rotateWithMinecart" };
const QSet<QString> kLanguage = { "lang", "mainHand", "skinCustomisation" };

/// "" = never copied.
QString categoryOf(const QString& key)
{
    if (key == "version") {
        return {};
    }
    if (key.startsWith("key_") || kControls.contains(key)) {
        return "controls";
    }
    if (key.startsWith("soundCategory_") || kSound.contains(key)) {
        return "sound";
    }
    if (key.startsWith("chat") || kChat.contains(key)) {
        return "chat";
    }
    if (key.startsWith("modelPart_") || kLanguage.contains(key)) {
        return "language";
    }
    if (kVideo.contains(key)) {
        return "video";
    }
    return "other";
}

// ---------------------------------------------------------------- versions

/// Minor version of a 1.x release ("1.20.4" → 20); 0 for alpha/beta/classic; -1 when unknown (snapshots, etc.).
int minorOf(const QString& version)
{
    static const QRegularExpression release("^1\\.(\\d+)");
    if (auto m = release.match(version); m.hasMatch()) {
        return m.captured(1).toInt();
    }
    static const QRegularExpression ancient("^(a|b|c|rd|inf)\\d");
    if (ancient.match(version).hasMatch()) {
        return 0;
    }
    static const QRegularExpression snapshot("^(\\d\\d)w(\\d\\d)");
    if (auto m = snapshot.match(version); m.hasMatch()) {
        const int year = m.captured(1).toInt();
        const int week = m.captured(2).toInt();
        if (year < 17 || (year == 17 && week < 43)) {
            return 12;  // close enough: old key format, upper-case language before 16w32
        }
        return 13;
    }
    return -1;
}

enum class KeyFormat { Unknown, Legacy, Modern };

KeyFormat formatOfFile(const Options& o)
{
    for (const auto& k : o.order) {
        if (!k.startsWith("key_")) {
            continue;
        }
        const QString v = o.values.value(k);
        if (v.startsWith("key.")) {
            return KeyFormat::Modern;
        }
        bool isInt = false;
        v.section(':', 0, 0).toInt(&isInt);
        if (isInt) {
            return KeyFormat::Legacy;
        }
    }
    return KeyFormat::Unknown;
}

KeyFormat formatOfVersion(const QString& version)
{
    const int minor = minorOf(version);
    if (minor < 0) {
        return KeyFormat::Unknown;
    }
    return minor < 13 ? KeyFormat::Legacy : KeyFormat::Modern;
}

// LWJGL 2 key codes (≤ 1.12) ↔ GLFW key names (1.13+). Mouse buttons were -100 + button.
const QHash<int, QString>& legacyToModern()
{
    static const QHash<int, QString> map = [] {
        QHash<int, QString> m{
            { 0, "key.keyboard.unknown" },     { 1, "key.keyboard.escape" },        { 12, "key.keyboard.minus" },
            { 13, "key.keyboard.equal" },      { 14, "key.keyboard.backspace" },    { 15, "key.keyboard.tab" },
            { 26, "key.keyboard.left.bracket" }, { 27, "key.keyboard.right.bracket" }, { 28, "key.keyboard.enter" },
            { 29, "key.keyboard.left.control" }, { 39, "key.keyboard.semicolon" },  { 40, "key.keyboard.apostrophe" },
            { 41, "key.keyboard.grave.accent" }, { 42, "key.keyboard.left.shift" }, { 43, "key.keyboard.backslash" },
            { 51, "key.keyboard.comma" },      { 52, "key.keyboard.period" },       { 53, "key.keyboard.slash" },
            { 54, "key.keyboard.right.shift" }, { 55, "key.keyboard.keypad.multiply" }, { 56, "key.keyboard.left.alt" },
            { 57, "key.keyboard.space" },      { 58, "key.keyboard.caps.lock" },    { 69, "key.keyboard.num.lock" },
            { 70, "key.keyboard.scroll.lock" }, { 74, "key.keyboard.keypad.subtract" }, { 78, "key.keyboard.keypad.add" },
            { 83, "key.keyboard.keypad.decimal" }, { 87, "key.keyboard.f11" },      { 88, "key.keyboard.f12" },
            { 156, "key.keyboard.keypad.enter" }, { 157, "key.keyboard.right.control" }, { 181, "key.keyboard.keypad.divide" },
            { 184, "key.keyboard.right.alt" }, { 197, "key.keyboard.pause" },       { 199, "key.keyboard.home" },
            { 200, "key.keyboard.up" },        { 201, "key.keyboard.page.up" },     { 203, "key.keyboard.left" },
            { 205, "key.keyboard.right" },     { 207, "key.keyboard.end" },         { 208, "key.keyboard.down" },
            { 209, "key.keyboard.page.down" }, { 210, "key.keyboard.insert" },      { 211, "key.keyboard.delete" },
            { 219, "key.keyboard.left.win" },  { 220, "key.keyboard.right.win" },
            { -100, "key.mouse.left" },        { -99, "key.mouse.right" },          { -98, "key.mouse.middle" },
        };
        const char* digits = "1234567890";  // codes 2..11
        for (int i = 0; i < 10; ++i) {
            m.insert(2 + i, QString("key.keyboard.%1").arg(digits[i]));
        }
        const QList<QPair<int, QString>> rows = { { 16, "qwertyuiop" }, { 30, "asdfghjkl" }, { 44, "zxcvbnm" } };
        for (const auto& [start, letters] : rows) {
            for (int i = 0; i < letters.size(); ++i) {
                m.insert(start + i, QString("key.keyboard.%1").arg(letters[i]));
            }
        }
        for (int i = 0; i < 10; ++i) {  // F1..F10 = 59..68
            m.insert(59 + i, QString("key.keyboard.f%1").arg(i + 1));
        }
        const int keypad[] = { 82, 79, 80, 81, 75, 76, 77, 71, 72, 73 };  // keypad 0..9
        for (int i = 0; i < 10; ++i) {
            m.insert(keypad[i], QString("key.keyboard.keypad.%1").arg(i));
        }
        for (int b = 3; b <= 7; ++b) {  // extra mouse buttons
            m.insert(-100 + b, QString("key.mouse.%1").arg(b + 1));
        }
        return m;
    }();
    return map;
}

const QHash<QString, int>& modernToLegacy()
{
    static const QHash<QString, int> map = [] {
        QHash<QString, int> m;
        const auto& fwd = legacyToModern();
        for (auto it = fwd.begin(); it != fwd.end(); ++it) {
            m.insert(it.value(), it.key());
        }
        return m;
    }();
    return map;
}

/// Converts one key binding value; returns false when the other format has no such key.
bool convertKey(QString& value, KeyFormat to)
{
    // Forge stores "<key>:<modifier>"; the modifier only exists in the modern format
    const QString key = value.section(':', 0, 0);
    if (to == KeyFormat::Modern) {
        bool ok = false;
        const int code = key.toInt(&ok);
        if (!ok) {
            return key.startsWith("key.");  // already modern
        }
        const auto& m = legacyToModern();
        if (!m.contains(code)) {
            return false;
        }
        value = m.value(code);
        return true;
    }
    if (!key.startsWith("key.")) {
        bool ok = false;
        key.toInt(&ok);
        return ok;  // already legacy
    }
    const auto& m = modernToLegacy();
    if (!m.contains(key)) {
        return false;
    }
    value = QString::number(m.value(key));
    return true;
}

// ---------------------------------------------------------------- instances and files

MinecraftInstance* findInstance(const QString& id)
{
    return id.isEmpty() ? nullptr : APPLICATION->instances()->getInstanceById(id);
}

QString gameVersionOf(MinecraftInstance* inst)
{
    auto* profile = inst->getPackProfile();
    if (!profile) {
        return {};
    }
    QString v = profile->getComponentVersion("net.minecraft");
    if (v.isEmpty()) {
        if (auto res = profile->reload(Net::Mode::Offline); !res) {
            qWarning() << "GameOptions: failed to load components of" << inst->id();
        }
        v = profile->getComponentVersion("net.minecraft");
    }
    return v;
}

/// The release a file was saved by, from its DataVersion, as a minor version (only the steps that matter here).
int minorOfDataVersion(const Options& o)
{
    bool ok = false;
    const int dv = o.get("version").toInt(&ok);
    if (!ok) {
        return -1;
    }
    if (dv >= 3105) return 19;  // 1.19: values written as JSON
    if (dv >= 2566) return 16;  // graphicsMode
    if (dv >= 1519) return 13;  // key names
    if (dv >= 819) return 11;   // lower-case language
    return 10;
}

/// Extra files that hold video settings; merged key by key like options.txt.
const QStringList kExtraTextFiles = { "optionsof.txt", "optionsshaders.txt" };
QChar separatorOf(const QString& file)
{
    return file == "optionsshaders.txt" ? QChar('=') : QChar(':');
}
const QString kSodium = "config/sodium-options.json";

QString backupName(const QString& path)
{
    return path + ".bak-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
}

/// Keeps the newest three backups of `path`.
void pruneBackups(const QString& path)
{
    const QFileInfo fi(path);
    QDir dir = fi.dir();
    auto list = dir.entryList({ fi.fileName() + ".bak-*" }, QDir::Files, QDir::Name);
    while (list.size() > 3) {
        dir.remove(list.takeFirst());
    }
}

QJsonObject deepMerge(QJsonObject base, const QJsonObject& over)
{
    for (auto it = over.begin(); it != over.end(); ++it) {
        if (it.value().isObject() && base.value(it.key()).isObject()) {
            base.insert(it.key(), deepMerge(base.value(it.key()).toObject(), it.value().toObject()));
        } else {
            base.insert(it.key(), it.value());
        }
    }
    return base;
}

/// One file to be written for one target.
struct Pending {
    QString path;
    QByteArray content;
};

}  // namespace

GameOptions::GameOptions(QObject* parent) : QObject(parent) {}

QVariantMap GameOptions::summary(const QString& id) const
{
    QVariantMap out;
    auto* inst = findInstance(id);
    if (!inst) {
        return out;
    }
    const QString root = inst->gameRoot();
    const Options o = readOptions(FS::PathCombine(root, "options.txt"));
    out["exists"] = o.exists;
    out["gameVersion"] = gameVersionOf(inst);

    QHash<QString, int> counts;
    int keybinds = 0;
    int skinParts = 0;
    for (const auto& k : o.order) {
        const QString cat = categoryOf(k);
        if (cat.isEmpty()) {
            continue;
        }
        counts[cat]++;
        if (k.startsWith("key_")) {
            keybinds++;
        }
        if (k.startsWith("modelPart_") && o.get(k) == "true") {
            skinParts++;
        }
    }
    QVariantMap cats;
    auto num = [&](const QString& k) -> QVariant {
        bool ok = false;
        const double d = o.get(k).toDouble(&ok);
        return ok ? QVariant(d) : QVariant();
    };

    QVariantMap controls{ { "count", counts.value("controls") }, { "keybinds", keybinds } };
    if (auto s = num("mouseSensitivity"); s.isValid()) {
        controls["sensitivity"] = qRound(s.toDouble() * 200);  // what the slider shows, in %
    }
    cats["controls"] = controls;

    QVariantMap video{ { "count", counts.value("video") } };
    if (auto v = num("renderDistance"); v.isValid()) {
        video["renderDistance"] = v.toInt();
    }
    if (auto v = num("guiScale"); v.isValid()) {
        video["guiScale"] = v.toInt();
    }
    if (auto v = num("fov"); v.isValid()) {
        video["fov"] = qRound(70 + v.toDouble() * 40);
    }
    QStringList extras;
    for (const auto& f : kExtraTextFiles) {
        if (QFile::exists(FS::PathCombine(root, f))) {
            extras << f;
        }
    }
    if (QFile::exists(FS::PathCombine(root, kSodium))) {
        extras << kSodium;
    }
    video["extras"] = extras;
    if (extras.contains("optionsshaders.txt")) {
        const QString pack = readOptions(FS::PathCombine(root, "optionsshaders.txt"), '=').get("shaderPack");
        if (!pack.isEmpty() && pack != "OFF" && pack != "(internal)") {
            video["shaderPack"] = pack;
        }
    }
    video["count"] = counts.value("video") + extras.size();
    cats["video"] = video;

    QVariantMap sound{ { "count", counts.value("sound") } };
    if (auto v = num("soundCategory_master"); v.isValid()) {
        sound["master"] = qRound(v.toDouble() * 100);
    }
    cats["sound"] = sound;

    cats["chat"] = QVariantMap{ { "count", counts.value("chat") } };

    QVariantMap language{ { "count", counts.value("language") }, { "skinParts", skinParts } };
    if (o.has("lang")) {
        const QString code = o.get("lang");
        const QLocale loc(code);
        QString name = loc.language() == QLocale::C ? code : loc.nativeLanguageName();
        if (!name.isEmpty()) {
            name[0] = name[0].toUpper();
        }
        language["lang"] = name;
    }
    cats["language"] = language;

    cats["other"] = QVariantMap{ { "count", counts.value("other") } };
    out["categories"] = cats;
    return out;
}

QVariantMap GameOptions::plan(const QString& sourceId, const QStringList& targetIds, const QStringList& categories) const
{
    return run(sourceId, targetIds, categories, nullptr);
}

QVariantMap GameOptions::apply(const QString& sourceId, const QStringList& targetIds, const QStringList& categories)
{
    QList<Written> written;
    auto result = run(sourceId, targetIds, categories, &written);
    if (!written.isEmpty()) {
        m_last = written;
        m_lastTargets = result.value("written").toInt();
        emit undoChanged();
    }
    return result;
}

QVariantMap GameOptions::run(const QString& sourceId,
                             const QStringList& targetIds,
                             const QStringList& categories,
                             QList<Written>* written)
{
    QVariantMap result{ { "ok", false }, { "targets", 0 }, { "changed", 0 }, { "converted", 0 }, { "skipped", 0 }, { "written", 0 } };
    auto* source = findInstance(sourceId);
    if (!source) {
        result["error"] = tr("The instance to copy from is gone.");
        return result;
    }
    const QSet<QString> cats(categories.begin(), categories.end());
    const QString srcRoot = source->gameRoot();
    const Options src = readOptions(FS::PathCombine(srcRoot, "options.txt"));
    const KeyFormat srcFormat = src.exists ? formatOfFile(src) : KeyFormat::Unknown;
    const KeyFormat srcKeys = srcFormat != KeyFormat::Unknown ? srcFormat : formatOfVersion(gameVersionOf(source));

    QStringList running;
    int targets = 0, changed = 0, converted = 0, skipped = 0, writtenTargets = 0;
    QStringList errors;

    for (const auto& id : targetIds) {
        auto* target = findInstance(id);
        if (!target || id == sourceId) {
            continue;
        }
        if (target->isRunning()) {  // the game rewrites options.txt when it quits
            running << target->name();
            continue;
        }
        targets++;
        const QString root = target->gameRoot();
        const QString tgtVersion = gameVersionOf(target);
        int tgtMinor = minorOf(tgtVersion);
        QList<Pending> files;

        // ---- options.txt
        if (src.exists) {
            const QString path = FS::PathCombine(root, "options.txt");
            Options tgt = readOptions(path);
            if (tgtMinor < 0) {
                tgtMinor = minorOfDataVersion(tgt);
            }
            const KeyFormat fileFormat = tgt.exists ? formatOfFile(tgt) : KeyFormat::Unknown;
            const KeyFormat tgtKeys = fileFormat != KeyFormat::Unknown ? fileFormat : formatOfVersion(tgtVersion);
            // A new modern file gets the source's lines and version: Minecraft's own fixers upgrade them.
            const bool letGameUpgrade = !tgt.exists && tgtKeys == KeyFormat::Modern && srcKeys == KeyFormat::Legacy;
            const bool convertKeys = !letGameUpgrade && srcKeys != KeyFormat::Unknown && tgtKeys != KeyFormat::Unknown &&
                                     srcKeys != tgtKeys;
            int here = 0;
            for (const auto& key : src.order) {
                const QString cat = categoryOf(key);
                if (cat.isEmpty() || !cats.contains(cat)) {
                    continue;
                }
                QString value = src.get(key);
                QString name = key;
                if (key.startsWith("key_") && convertKeys) {
                    if (!convertKey(value, tgtKeys)) {
                        skipped++;
                        continue;
                    }
                    converted++;
                }
                if (!letGameUpgrade && tgtMinor >= 0) {
                    if (key == "lang" && value.size() == 5) {
                        value = tgtMinor >= 11 ? value.toLower() : value.left(3).toLower() + value.mid(3).toUpper();
                    } else if (key == "mainHand" && tgtMinor < 19 && value.size() > 2 && value.startsWith('"')) {
                        value = value.mid(1, value.size() - 2);
                    } else if (key == "mainHand" && tgtMinor >= 19 && !value.startsWith('"')) {
                        value = '"' + value + '"';
                    } else if (key == "graphicsMode" && tgtMinor < 16) {
                        name = "fancyGraphics";
                        value = value.toInt() >= 1 ? "true" : "false";
                    } else if (key == "fancyGraphics" && tgtMinor >= 16) {
                        if (src.has("graphicsMode")) {
                            continue;  // both present: graphicsMode wins
                        }
                        name = "graphicsMode";
                        value = value == "true" ? "1" : "0";
                    }
                }
                if (tgt.get(name) != value || !tgt.has(name)) {
                    tgt.set(name, value);
                    here++;
                }
            }
            if (!tgt.exists && here > 0 && src.has("version") && (letGameUpgrade || !convertKeys)) {
                // Minecraft treats a file without version as ancient and runs every fixer over it
                Options withVersion;
                withVersion.set("version", src.get("version"));
                for (const auto& k : tgt.order) {
                    withVersion.set(k, tgt.get(k));
                }
                tgt = withVersion;
            }
            if (here > 0) {
                changed += here;
                files.append({ path, serialize(tgt) });
            }
        }

        // ---- video extras: OptiFine, shaders, Sodium
        if (cats.contains("video")) {
            for (const auto& f : kExtraTextFiles) {
                const Options extra = readOptions(FS::PathCombine(srcRoot, f), separatorOf(f));
                if (!extra.exists) {
                    continue;
                }
                const QString path = FS::PathCombine(root, f);
                Options tgt = readOptions(path, separatorOf(f));
                int here = 0;
                for (const auto& k : extra.order) {
                    if (tgt.get(k) != extra.get(k) || !tgt.has(k)) {
                        tgt.set(k, extra.get(k));
                        here++;
                    }
                }
                if (here > 0) {
                    changed += here;
                    files.append({ path, serialize(tgt) });
                }
            }
            QFile sodium(FS::PathCombine(srcRoot, kSodium));
            if (sodium.open(QIODevice::ReadOnly)) {
                const auto srcJson = QJsonDocument::fromJson(sodium.readAll()).object();
                const QString path = FS::PathCombine(root, kSodium);
                QJsonObject tgtJson;
                if (QFile tf(path); tf.open(QIODevice::ReadOnly)) {
                    tgtJson = QJsonDocument::fromJson(tf.readAll()).object();
                }
                const auto merged = deepMerge(tgtJson, srcJson);
                if (!srcJson.isEmpty() && merged != tgtJson) {
                    changed++;
                    files.append({ path, QJsonDocument(merged).toJson(QJsonDocument::Indented) });
                }
            }
        }

        if (!written || files.isEmpty()) {
            continue;
        }
        bool any = false;
        for (const auto& p : files) {
            QString backup;
            if (QFile::exists(p.path)) {
                backup = backupName(p.path);
                QFile::remove(backup);
                if (!QFile::copy(p.path, backup)) {
                    errors << tr("Couldn't back up %1").arg(QDir::toNativeSeparators(p.path));
                    continue;
                }
            } else {
                QDir().mkpath(QFileInfo(p.path).absolutePath());
            }
            QSaveFile out(p.path);
            if (!out.open(QIODevice::WriteOnly) || out.write(p.content) != p.content.size() || !out.commit()) {
                errors << tr("Couldn't write %1").arg(QDir::toNativeSeparators(p.path));
                continue;
            }
            written->append({ p.path, backup });
            if (!backup.isEmpty()) {
                pruneBackups(p.path);
            }
            any = true;
        }
        if (any) {
            writtenTargets++;
        }
    }

    result["ok"] = errors.isEmpty();
    result["error"] = errors.join('\n');
    result["targets"] = targets;
    result["changed"] = changed;
    result["converted"] = converted;
    result["skipped"] = skipped;
    result["running"] = running;
    result["written"] = writtenTargets;
    result["sourceMissing"] = !src.exists;
    return result;
}

bool GameOptions::undo()
{
    bool ok = true;
    // newest first, in case one file was written twice
    for (auto it = m_last.crbegin(); it != m_last.crend(); ++it) {
        if (it->backup.isEmpty()) {
            ok = QFile::remove(it->path) && ok;
            continue;
        }
        QFile::remove(it->path);
        ok = QFile::copy(it->backup, it->path) && ok;
    }
    m_last.clear();
    m_lastTargets = 0;
    emit undoChanged();
    return ok;
}

QStringList GameOptions::allOtherInstances(const QString& exceptId) const
{
    auto* list = APPLICATION->instances();
    QList<MinecraftInstance*> all;
    for (int i = 0; i < list->count(); ++i) {
        if (list->at(i)->id() != exceptId) {
            all << list->at(i);
        }
    }
    std::sort(all.begin(), all.end(), [](auto* a, auto* b) { return a->lastLaunch() > b->lastLaunch(); });
    QStringList ids;
    for (auto* i : all) {
        ids << i->id();
    }
    return ids;
}
