// SPDX-License-Identifier: GPL-3.0-only
#include "CrashRules.h"

#include <QRegularExpression>
#include <QSet>

namespace CrashRules {

namespace {

using RX = QRegularExpression;

QString quote(const QString& line)
{
    QString s = line.trimmed();
    s.remove(QRegularExpression(QStringLiteral("§.")));  // Minecraft colour codes from NeoForge messages
    return s.size() > 400 ? s.left(400) + QStringLiteral("…") : s;
}

/// "'Sodium' (sodium)" -> id "sodium", name "Sodium"; "fabric-api" -> both "fabric-api"
void splitMod(const QString& text, QString& id, QString& name)
{
    static const RX quoted(QStringLiteral(R"(^'(.+)' \(([^)\s]+)\)$)"));
    const auto m = quoted.match(text.trimmed());
    if (m.hasMatch()) {
        name = m.captured(1);
        id = m.captured(2);
    } else {
        id = text.trimmed();
        id.remove('\'');
        id.remove('`');
        name = id;
    }
}

int firstNumber(const QString& text)
{
    static const RX num(QStringLiteral(R"((\d+))"));
    const auto m = num.match(text);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

/// Java major from a Fabric range text ("version 17 or later", "any version after 16", ">=21")
int javaFromRange(const QString& range)
{
    int n = firstNumber(range);
    if (n == 1) {  // "1.8" style
        static const RX legacy(QStringLiteral(R"(1\.(\d+))"));
        const auto m = legacy.match(range);
        n = m.hasMatch() ? m.captured(1).toInt() : 0;
    }
    if (n > 0 && range.contains(QStringLiteral("after"))) {
        n++;
    }
    return n >= 5 && n < 100 ? n : 0;
}

QString jarIn(const QString& line)
{
    // A path into a mods folder, ending in .jar (Unix or Windows separators)
    static const RX path(QStringLiteral(R"((?:^|[^\w.\-])mods[/\\]((?:[^\s'"\[\]()<>,/\\]+[/\\])*[^\s'"\[\]()<>,/\\]+?\.jar)\b)"));
    const auto m = path.match(line);
    if (!m.hasMatch()) {
        return {};
    }
    QString p = m.captured(1);
    return p.mid(qMax(p.lastIndexOf('/'), p.lastIndexOf('\\')) + 1);
}

void add(QList<Finding>& out, Finding f)
{
    for (auto& existing : out) {
        if (existing.kind == f.kind && existing.mod == f.mod && existing.dependency == f.dependency && existing.files == f.files &&
            existing.javaMajor == f.javaMajor) {
            for (const auto& e : f.evidence) {
                if (!existing.evidence.contains(e) && existing.evidence.size() < 6) {
                    existing.evidence << e;
                }
            }
            return;
        }
    }
    out << std::move(f);
}

bool isStackLine(const QString& line)
{
    static const RX at(QStringLiteral(R"(^\s*at \S+)"));
    return at.match(line).hasMatch() || line.trimmed().startsWith(QStringLiteral("...")) ||
           line.trimmed().startsWith(QStringLiteral("Caused by:")) || line.trimmed().startsWith(QStringLiteral("Suppressed:"));
}

void findExcerpt(const QStringList& lines, Diagnosis& d)
{
    static const RX exception(QStringLiteral(R"((?:Exception in thread "|Caused by: |(?:^|[\s\]:])(?:[a-zA-Z_][\w$]*\.)+[\w$]*(?:Exception|Error)\b))"));
    int header = -1;
    int span = 40;
    for (int i = 0; i < lines.size() && header < 0; i++) {
        if (lines[i].contains(QStringLiteral("---- Minecraft Crash Report ----"))) {
            header = i;
            span = 45;
        }
    }
    if (header < 0) {
        for (int i = 0; i < lines.size() && header < 0; i++) {
            const QString& l = lines[i];
            if (l.contains(QStringLiteral("Exception in thread")) || l.contains(QStringLiteral("Unreported exception thrown")) ||
                l.contains(QStringLiteral("FormattedException")) || l.contains(QStringLiteral("Incompatible mods found")) ||
                l.contains(QStringLiteral("mandatory dependencies"))) {
                header = i;
            }
        }
    }
    if (header < 0) {
        // The last stack trace in the log: crashes end the log, earlier traces are usually harmless.
        int last = -1;
        for (int i = lines.size() - 1; i >= 0; i--) {
            if (isStackLine(lines[i]) && !lines[i].trimmed().startsWith(QStringLiteral("..."))) {
                last = i;
                break;
            }
        }
        if (last >= 0) {
            int i = last;
            while (i > 0 && isStackLine(lines[i - 1])) {
                i--;
            }
            header = i > 0 ? i - 1 : i;
        } else {
            for (int i = 0; i < lines.size(); i++) {
                if (exception.match(lines[i]).hasMatch()) {
                    header = i;
                    break;
                }
            }
        }
    }
    if (header < 0) {
        // No exception at all: the end of the log is the best we have.
        header = qMax(0, int(lines.size()) - 30);
        span = 30;
    }
    const int start = header;  // not earlier: lines before the error (warnings, "conflict" chatter) would read as context
    int end = qMin(int(lines.size()), header + span);
    // Stop after the stack trace once the log moves on (keeps unrelated lines out).
    for (int i = header + 3; i < end; i++) {
        if (!isStackLine(lines[i]) && i > header + 8 && !isStackLine(lines[i - 1]) && lines[i].trimmed().isEmpty()) {
            end = i;
            break;
        }
    }
    d.excerptStart = start;
    for (int i = start; i < end; i++) {
        QString l = lines[i];
        if (l.endsWith('\r')) {
            l.chop(1);
        }
        d.excerpt << (l.size() > 600 ? l.left(600) + QStringLiteral("…") : l);
    }
    while (!d.excerpt.isEmpty() && d.excerpt.last().trimmed().isEmpty()) {
        d.excerpt.removeLast();
    }

    // Mods the trace mentions. Not causes: shown only under a caveat.
    static const RX transformer(QStringLiteral(R"(\bat (?:[A-Z][A-Z \-]*/)?([a-z][a-z0-9_\-]{1,63})@[^/\s]+/)"));
    static const RX jar(QStringLiteral(R"(\[([^\[\]:/\\\s]+?\.jar)[%:!\]])"));
    static const RX mixin(QStringLiteral(R"(\$[a-z]{3}\d{3}\$([a-z][a-z0-9_]{1,63})\$)"));
    static const QSet<QString> system = {
        "minecraft", "forge", "neoforge", "fml", "fmlcore", "fmlloader", "fabricloader", "java", "javafmllanguage",
        "lowcodelanguage", "mclanguage", "mixin", "mixinextras", "securejarhandler", "bootstraplauncher", "modlauncher",
        "eventbus", "coremods", "loader", "client", "server", "quilt_loader",
    };
    static const QStringList systemJars = { "client", "minecraft", "forge-", "neoforge-", "fmlcore", "fmlloader", "fabric-loader",
                                            "quilt-loader", "intermediary", "modlauncher", "securejarhandler", "bootstraplauncher",
                                            "eventbus", "coremods", "mixin", "sponge-mixin", "javafml", "lowcode", "mclanguage",
                                            "srgutils", "server", "NewLaunch", "launchwrapper", "lwjgl", "guava", "netty",
                                            "datafixerupper", "authlib", "brigadier", "log4j", "ForgeAutoRenamingTool" };
    QStringList mentioned;
    auto note = [&mentioned](const QString& name) {
        if (!mentioned.contains(name) && mentioned.size() < 12) {
            mentioned << name;
        }
    };
    for (const auto& l : d.excerpt) {
        if (!isStackLine(l) && !l.contains(QStringLiteral("Mixin"))) {
            continue;
        }
        for (auto it = transformer.globalMatch(l); it.hasNext();) {
            const auto id = it.next().captured(1);
            if (!system.contains(id)) {
                note(id);
            }
        }
        for (auto it = mixin.globalMatch(l); it.hasNext();) {
            const auto id = it.next().captured(1);
            if (!system.contains(id)) {
                note(id);
            }
        }
        for (auto it = jar.globalMatch(l); it.hasNext();) {
            const auto name = it.next().captured(1);
            bool skip = false;
            for (const auto& s : systemJars) {
                skip = skip || name.startsWith(s, Qt::CaseInsensitive);
            }
            if (!skip) {
                note(name);
            }
        }
    }
    d.mentioned = mentioned;
}

}  // namespace

int javaForClassVersion(int classVersion)
{
    return classVersion >= 49 ? classVersion - 44 : 0;
}

Diagnosis diagnose(const QStringList& input)
{
    Diagnosis d;
    QList<Finding>& out = d.findings;

    // Fabric / Quilt: "Mod 'Iris' (iris) 1.6.4 requires version 0.5.0 or later of mod 'Sodium' (sodium), but only the
    // wrong version is present: 0.4.10!" and "... requires any version of fabric-api, which is missing!"
    // ("recommends" and "conflicts with" lines are warnings the loader lets the game start with: never matched.)
    static const RX fabricDep(QStringLiteral(
        R"((?:(?:Mod|Library) )?('(?:[^']|'(?! \())+' \([^)\s]+\)|[\w.\-]+)(?: (\S+))? requires (.+?) of (?:mod |library )?('(?:[^']|'(?! \())+' \([^)\s]+\)|'?[\w.\-]+'?), (which is missing|but only the wrong versions? (?:is|are) present: (.+?))!\s*$)"));
    static const RX fabricBreaks(QStringLiteral(
        R"((?:(?:Mod|Library) )?('(?:[^']|'(?! \())+' \([^)\s]+\)|[\w.\-]+)(?: (\S+))? is incompatible with (.+?) of (?:mod |library )?('(?:[^']|'(?! \())+' \([^)\s]+\)|'?[\w.\-]+'?), but (?:a )?matching versions? (?:is|are) present: (.+?)!\s*$)"));
    // Old Fabric (≤0.11): "Could not find required mod: modmenu requires {fabric @ [*]}"
    static const RX fabricOld(QStringLiteral(R"(Could not find required mod: ([\w.\-]+) requires \{([\w.\-]+) @ \[([^\]]*)\]\})"));
    // Forge / NeoForge (1.13+): under "Missing or unsupported mandatory dependencies:"
    static const RX forgeDep(QStringLiteral(
        R"(Mod ID: '([^']+)', Requested by: '([^']+)', Expected range: '([^']*)', Actual version: '([^']*)')"));
    // Forge 1.12: "MissingModsException: Mod jeresources (Just Enough Resources) requires [jei@[4.15.0,)]"
    static const RX forgeOld(QStringLiteral(R"(MissingModsException: Mod ([\w.\-]+) \(([^)]*)\) requires \[([\w.\-]+)@?(.*)\]\s*$)"));
    // NeoForge loading screen text in the log: "Mod jei requires cloth_config 11 or above" + "Currently, cloth_config is not installed"
    static const RX neoDep(QStringLiteral(R"(Mod [`']?([\w.\-]+)[`']? requires [`']?([\w.\-]+)[`']? (.+?)\s*$)"));
    static const RX neoMissing(QStringLiteral(R"(Currently, [`']?([\w.\-]+)[`']? is not installed)"));
    // Duplicates the loader refuses
    static const RX forgeDup(QStringLiteral(R"(Mod ID: '([^']+)' from mod files: (.+?)\s*$)"));
    static const RX forgeDupOld(QStringLiteral(R"(Found a duplicate mod ([\w.\-]+) at \[(.+)\])"));
    static const RX fabricDup(QStringLiteral(R"(Exception: [Dd]uplicate mod:? ([\w.\-]+))"));
    // JVM
    static const RX oom(QStringLiteral(R"((?:^|[\s:\]])java\.lang\.OutOfMemoryError(?::\s*(.*))?$)"));
    static const RX classVersion(QStringLiteral(
        R"(UnsupportedClassVersionError.*class file version (\d+)(?:\.\d+)?\))"));
    static const RX classVersionTail(QStringLiteral(R"(recognizes class file versions up to (\d+))"));
    static const RX majorMinor(QStringLiteral(R"(UnsupportedClassVersionError.*Unsupported major\.minor version (\d+))"));
    static const RX requiresJava(QStringLiteral(R"(\brequires Java (\d+)\b)"), RX::CaseInsensitiveOption);
    static const RX graphics(QStringLiteral(
        R"(Pixel format not accelerated|GLFW error:? 6554[23]\b|does not appear to support OpenGL|Failed to find a suitable pixel format)"));
    static const RX zip(QStringLiteral(
        R"(java\.util\.zip\.Zip(?:Exception|Error)|zip END header not found|zip file is empty|invalid LOC header|error in opening zip file)"));

    enum class Section { None, Mandatory, Optional, Duplicates } section = Section::None;

    for (int i = 0; i < input.size(); i++) {
        QString line = input[i];
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.size() > 4000) {
            continue;  // giant one-line dumps (mod lists, JSON) never hold an error message
        }

        // ---- Forge sections
        if (line.contains(QStringLiteral("Missing or unsupported mandatory dependencies"))) {
            section = Section::Mandatory;
            continue;
        }
        if (line.contains(QStringLiteral("optional dependencies"))) {
            section = Section::Optional;
            continue;
        }
        if (line.contains(QStringLiteral("Found duplicate mods"))) {
            section = Section::Duplicates;
            // Kept only for the entries where the loader names the mod and its files
            int j = i + 1;
            for (; j < input.size() && j < i + 40; j++) {
                const auto m = forgeDup.match(input[j]);
                if (!m.hasMatch()) {
                    break;
                }
                Finding g;
                g.kind = "duplicate";
                g.dependency = m.captured(1);
                g.evidence << quote(line) << quote(input[j]);
                for (auto file : m.captured(2).split(',', Qt::SkipEmptyParts)) {
                    file = file.trimmed();
                    g.files << file.mid(qMax(file.lastIndexOf('/'), file.lastIndexOf('\\')) + 1);
                }
                add(out, g);
            }
            i = j - 1;
            section = Section::None;
            continue;
        }
        if (section == Section::Mandatory) {
            if (const auto m = forgeDep.match(line); m.hasMatch()) {
                Finding f;
                const QString actual = m.captured(4);
                const bool missing = actual.isEmpty() || actual.contains(QStringLiteral("MISSING"), Qt::CaseInsensitive);
                f.kind = missing ? "missing-dependency" : "wrong-dependency";
                f.dependency = m.captured(1);
                f.dependencyName = f.dependency;
                f.mod = m.captured(2);
                f.range = m.captured(3);
                f.found = missing ? QString() : actual;
                f.evidence << quote(line);
                if (f.dependency == "java") {
                    f.kind = "java";
                    f.javaMajor = javaFromRange(f.range);
                    if (!f.javaMajor) {
                        continue;
                    }
                }
                add(out, f);
                continue;
            }
            if (!line.trimmed().isEmpty()) {
                section = Section::None;
            }
        } else if (section == Section::Optional) {
            if (!line.contains(QStringLiteral("Mod ID:")) && !line.trimmed().isEmpty()) {
                section = Section::None;
            }
            continue;  // optional dependencies never stop the game
        }

        // ---- Fabric / Quilt resolution
        if (line.contains(QStringLiteral(" requires ")) && line.contains('!')) {
            if (const auto m = fabricDep.match(line); m.hasMatch()) {
                Finding f;
                QString modId, modName, depId, depName;
                splitMod(m.captured(1), modId, modName);
                splitMod(m.captured(4), depId, depName);
                const bool missing = m.captured(5).startsWith(QStringLiteral("which is missing"));
                f.mod = modName;
                f.dependency = depId;
                f.dependencyName = depName;
                f.range = m.captured(3);
                f.found = missing ? QString() : m.captured(6);
                f.evidence << quote(line);
                if (depId == "java") {
                    f.kind = "java";
                    f.javaMajor = javaFromRange(f.range);
                    f.javaCurrent = firstNumber(f.found);
                    if (f.javaCurrent == 1) {
                        f.javaCurrent = javaFromRange(f.found);
                    }
                    f.javaTooNew = f.javaCurrent > 0 && f.javaMajor > 0 && f.javaCurrent > f.javaMajor &&
                                   (f.range.contains(QStringLiteral("before")) || f.range.contains(QStringLiteral("earlier")));
                    if (!f.javaMajor) {
                        continue;
                    }
                } else {
                    f.kind = missing ? "missing-dependency" : "wrong-dependency";
                }
                add(out, f);
                continue;
            }
        }
        if (line.contains(QStringLiteral("is incompatible with"))) {
            if (const auto m = fabricBreaks.match(line); m.hasMatch()) {
                Finding f;
                QString modId, modName, depId, depName;
                splitMod(m.captured(1), modId, modName);
                splitMod(m.captured(4), depId, depName);
                f.kind = "incompatible";
                f.mod = modName;
                f.dependency = depId;
                f.dependencyName = depName;
                f.range = m.captured(3);
                f.found = m.captured(5);
                f.evidence << quote(line);
                add(out, f);
                continue;
            }
        }
        if (const auto m = fabricOld.match(line); m.hasMatch()) {
            Finding f;
            f.kind = "missing-dependency";
            f.mod = m.captured(1);
            f.dependency = m.captured(2);
            f.dependencyName = f.dependency;
            f.range = m.captured(3);
            f.evidence << quote(line);
            add(out, f);
            continue;
        }
        if (const auto m = forgeOld.match(line); m.hasMatch()) {
            Finding f;
            f.kind = "missing-dependency";
            f.mod = m.captured(2).isEmpty() ? m.captured(1) : m.captured(2);
            f.dependency = m.captured(3);
            f.dependencyName = f.dependency;
            f.range = m.captured(4);
            f.evidence << quote(line);
            add(out, f);
            continue;
        }
        if (line.contains(QStringLiteral(" requires ")) && i + 1 < input.size()) {
            const QString clean = QString(line).remove(QRegularExpression(QStringLiteral("§.")));
            const QString next = QString(input[i + 1]).remove(QRegularExpression(QStringLiteral("§.")));
            const auto m = neoDep.match(clean);
            const auto n = neoMissing.match(next);
            if (m.hasMatch() && n.hasMatch() && n.captured(1) == m.captured(2)) {
                Finding f;
                f.kind = "missing-dependency";
                f.mod = m.captured(1);
                f.dependency = m.captured(2);
                f.dependencyName = f.dependency;
                f.range = m.captured(3);
                f.evidence << quote(line) << quote(input[i + 1]);
                add(out, f);
                i++;
                continue;
            }
        }
        if (const auto m = forgeDupOld.match(line); m.hasMatch()) {
            Finding f;
            f.kind = "duplicate";
            f.dependency = m.captured(1);
            for (auto file : m.captured(2).split(',', Qt::SkipEmptyParts)) {
                file = file.trimmed();
                f.files << file.mid(qMax(file.lastIndexOf('/'), file.lastIndexOf('\\')) + 1);
            }
            f.evidence << quote(line);
            add(out, f);
            continue;
        }
        if (const auto m = fabricDup.match(line); m.hasMatch()) {
            Finding f;
            f.kind = "duplicate";
            f.dependency = m.captured(1);
            f.evidence << quote(line);
            add(out, f);
            continue;
        }

        // ---- JVM memory
        if (line.contains(QStringLiteral("OutOfMemoryError"))) {
            if (const auto m = oom.match(line); m.hasMatch()) {
                const QString what = m.captured(1);
                // Thread limits are an OS setting; more heap does not help there.
                if (!what.contains(QStringLiteral("native thread"))) {
                    Finding f;
                    f.kind = "memory";
                    f.evidence << quote(line);
                    add(out, f);
                }
                continue;
            }
        }
        if (line.contains(QStringLiteral("Could not reserve enough space for")) && line.contains(QStringLiteral("heap"))) {
            Finding f;
            f.kind = "memory-reserve";
            f.evidence << quote(line);
            add(out, f);
            continue;
        }
        if (line.contains(QStringLiteral("There is insufficient memory for the Java Runtime Environment to continue"))) {
            Finding f;
            f.kind = "memory-system";
            f.evidence << quote(line);
            add(out, f);
            continue;
        }

        // ---- Java version
        if (line.contains(QStringLiteral("UnsupportedClassVersionError"))) {
            Finding f;
            f.kind = "java";
            if (const auto m = classVersion.match(line); m.hasMatch()) {
                f.javaMajor = javaForClassVersion(m.captured(1).toInt());
                if (const auto t = classVersionTail.match(line); t.hasMatch()) {
                    f.javaCurrent = javaForClassVersion(t.captured(1).toInt());
                }
            } else if (const auto mm = majorMinor.match(line); mm.hasMatch()) {
                f.javaMajor = javaForClassVersion(mm.captured(1).toInt());
            }
            if (f.javaMajor > 0) {
                f.evidence << quote(line);
                add(out, f);
            }
            continue;
        }
        if (line.contains(QStringLiteral("AppClassLoader cannot be cast to class java.net.URLClassLoader")) ||
            line.contains(QStringLiteral("AppClassLoader cannot be cast to java.net.URLClassLoader"))) {
            // LaunchWrapper (Minecraft ≤1.12, Forge ≤1.16) on Java 9+: it can only run on Java 8.
            Finding f;
            f.kind = "java";
            f.javaMajor = 8;
            f.javaTooNew = true;
            f.evidence << quote(line);
            add(out, f);
            continue;
        }
        if (line.contains(QStringLiteral("requires"), Qt::CaseInsensitive) && line.contains(QStringLiteral("java"), Qt::CaseInsensitive)) {
            if (const auto m = requiresJava.match(line); m.hasMatch()) {
                Finding f;
                f.kind = "java";
                f.javaMajor = m.captured(1).toInt();
                if (f.javaMajor >= 5 && f.javaMajor < 100) {
                    f.evidence << quote(line);
                    add(out, f);
                }
                continue;
            }
        }

        // ---- Graphics driver
        if (const auto m = graphics.match(line); m.hasMatch()) {
            Finding f;
            f.kind = "graphics";
            f.evidence << quote(line);
            add(out, f);
            continue;
        }

        // ---- Unreadable mod archive
        if (zip.match(line).hasMatch()) {
            QString file = jarIn(line);
            int near = i;
            for (int k : { i - 1, i + 1, i - 2 }) {
                if (file.isEmpty() && k >= 0 && k < input.size()) {
                    file = jarIn(input[k]);
                    near = k;
                }
            }
            if (!file.isEmpty()) {
                Finding f;
                f.kind = "corrupt-file";
                f.files << file;
                if (near < i) {
                    f.evidence << quote(input[near]);
                }
                f.evidence << quote(line);
                if (near > i) {
                    f.evidence << quote(input[near]);
                }
                add(out, f);
            }
            continue;
        }
    }

    findExcerpt(input, d);
    return d;
}

QVariantMap toVariant(const Finding& f)
{
    return {
        { "kind", f.kind },
        { "evidence", f.evidence },
        { "mod", f.mod },
        { "dependency", f.dependency },
        { "dependencyName", f.dependencyName },
        { "range", f.range },
        { "found", f.found },
        { "files", f.files },
        { "javaMajor", f.javaMajor },
        { "javaCurrent", f.javaCurrent },
        { "javaTooNew", f.javaTooNew },
    };
}

QVariantMap toVariant(const Diagnosis& d)
{
    QVariantList findings;
    for (const auto& f : d.findings) {
        findings << toVariant(f);
    }
    return {
        { "findings", findings },
        { "excerpt", d.excerpt.join('\n') },
        { "excerptStart", d.excerptStart },
        { "mentioned", d.mentioned },
    };
}

}  // namespace CrashRules
