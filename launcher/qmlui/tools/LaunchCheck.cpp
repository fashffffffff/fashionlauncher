// SPDX-License-Identifier: GPL-3.0-only
#include "LaunchCheck.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentMap>

#include "Application.h"
#include "FileSystem.h"
#include "Version.h"
#include "java/JavaVersion.h"
#include "minecraft/LaunchProfile.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

#include "ModScan.h"

namespace LaunchCheck {
namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ToolsBridge", text);
}
QString trn(const char* text, int n)
{
    return QCoreApplication::translate("ToolsBridge", text, nullptr, n);
}

QString loaderName(const QString& key)
{
    if (key == "fabric") {
        return QStringLiteral("Fabric");
    }
    if (key == "quilt") {
        return QStringLiteral("Quilt");
    }
    if (key == "forge") {
        return QStringLiteral("Forge");
    }
    if (key == "neoforge") {
        return QStringLiteral("NeoForge");
    }
    return key;
}

QVariantMap problem(const QString& kind, const QString& title, const QString& detail, const QString& fixLabel, const QString& fixAction,
                    const QVariant& fixArg, bool autoFix)
{
    return { { "kind", kind },           { "title", title },   { "detail", detail },   { "fixLabel", fixLabel },
             { "fixAction", fixAction }, { "fixArg", fixArg }, { "autoFix", autoFix } };
}

/// "cloth-config" -> "Cloth Config"
QString prettyId(const QString& id)
{
    QStringList words = id.split(QRegularExpression("[-_]"), Qt::SkipEmptyParts);
    for (auto& w : words) {
        w[0] = w[0].toUpper();
    }
    return words.join(' ');
}

bool isFabricApiPart(const QString& id)
{
    static const QRegularExpression module(R"(^fabric-.+-v\d+$)");
    return id == "fabric-api" || id == "fabric" || id == "fabric-api-base" || module.match(id).hasMatch();
}

void checkJava(MinecraftInstance* inst, const QString& mc, QVariantList& out)
{
    auto* settings = inst->settings();
    if (settings->get("IgnoreJavaCompatibility").toBool()) {
        return;
    }
    const QString javaPathSetting = settings->get("JavaPath").toString();
    // With automatic Java the launcher picks a matching Java itself
    if (APPLICATION->settings()->get("AutomaticJavaSwitch").toBool() &&
        !(settings->get("OverrideJavaLocation").toBool() && QFileInfo::exists(javaPathSetting))) {
        return;
    }
    // Only trust the stored version when it belongs to the Java that is set now (same signature as CheckJava)
    const QString javaPath = FS::ResolveExecutable(javaPathSetting);
    const QString realJavaPath = QStandardPaths::findExecutable(javaPath);
    if (realJavaPath.isEmpty()) {
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(QFileInfo(realJavaPath).lastModified().toMSecsSinceEpoch()));
    hash.addData(javaPath.toUtf8());
    if (QString(hash.result().toHex()) != settings->get("JavaSignature").toString()) {
        return;
    }
    const QString stored = settings->get("JavaVersion").toString();
    if (stored.isEmpty()) {
        return;
    }
    auto profile = inst->getPackProfile()->getProfile();
    if (!profile) {
        return;
    }
    const auto majors = profile->getCompatibleJavaMajors();
    if (majors.isEmpty()) {
        return;
    }
    const int need = *std::min_element(majors.begin(), majors.end());
    const int have = JavaVersion(stored).major();
    if (have <= 0 || have >= need) {
        return;
    }
    out << problem("java", tr("Minecraft %1 needs Java %2 or newer").arg(mc).arg(need),
                   tr("This instance is set to use Java %1, which is too old to start the game.").arg(have), tr("Choose Java"), "settings",
                   QVariant(), false);
}

struct ModFile {
    QString path;
    ModScan::Jar jar;
    int activeIndex = -1;  // descriptor the loader will read: 0 fabric, 1 quilt, 2 forge, 3 neoforge
    bool incompatible = false;
    QString name;
    const ModScan::Descriptor* active() const
    {
        switch (activeIndex) {
            case 0:
                return &jar.fabric;
            case 1:
                return &jar.quilt;
            case 2:
                return &jar.forge;
            case 3:
                return &jar.neoforge;
            default:
                return nullptr;
        }
    }
};

void checkMods(MinecraftInstance* inst, const QString& loader, const QString& mc, QVariantList& out)
{
    const QDir dir(inst->modsRoot());
    QStringList paths;
    for (const auto& info : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (info.suffix().compare("jar", Qt::CaseInsensitive) == 0) {
            paths << info.absoluteFilePath();
        }
    }
    if (paths.isEmpty()) {
        return;
    }
    const QList<ModScan::Jar> jars = QtConcurrent::blockingMapped(paths, [](const QString& p) { return ModScan::scan(p); });

    const bool fabricLike = loader == "fabric" || loader == "quilt";
    const bool release = ModScan::isRelease(mc);
    const bool neoReadsModsToml = release && Version(mc) < Version("1.20.5");

    QList<ModFile> found;
    bool connector = false;  // Sinytra Connector lets (Neo)Forge load Fabric mods
    for (qsizetype i = 0; i < paths.size(); ++i) {
        for (const auto* d : { &jars[i].forge, &jars[i].neoforge }) {
            connector = connector || d->ids.contains("connector");
        }
    }
    for (qsizetype i = 0; i < paths.size(); ++i) {
        ModFile m{ paths[i], jars[i] };
        const auto& j = m.jar;
        if (!j.fabric.present && !j.quilt.present && !j.forge.present && !j.neoforge.present) {
            continue;  // a library or something we can't tell anything about
        }
        for (const auto* d : { &j.fabric, &j.quilt, &j.neoforge, &j.forge }) {
            if (d->parsed && !d->name.isEmpty()) {
                m.name = d->name;
                break;
            }
        }
        if (m.name.isEmpty()) {
            m.name = QFileInfo(m.path).completeBaseName();
        }
        if (loader == "fabric") {
            m.activeIndex = j.fabric.present ? 0 : -1;
        } else if (loader == "quilt") {
            m.activeIndex = j.quilt.present ? 1 : j.fabric.present ? 0 : -1;
        } else if (loader == "forge") {
            m.activeIndex = j.forge.present ? 2 : -1;
        } else if (loader == "neoforge") {
            if (j.neoforge.present) {
                m.activeIndex = 3;
            } else if (j.forge.present) {
                if (!release) {
                    continue;  // snapshot: can't tell whether NeoForge still reads mods.toml
                }
                m.activeIndex = neoReadsModsToml ? 2 : -1;
            }
        }
        if (m.activeIndex < 0) {
            if (!fabricLike && connector && (j.fabric.present || j.quilt.present)) {
                continue;
            }
            m.incompatible = true;
        }
        found << m;
    }
    const QList<ModFile> mods = std::move(found);

    // 1. Mods for another loader
    QList<const ModFile*> wrongLoader;
    for (const auto& m : mods) {
        if (m.incompatible) {
            wrongLoader << &m;
        }
    }
    auto madeFor = [](const ModScan::Jar& j) {
        QStringList l;
        if (j.fabric.present) {
            l << "Fabric";
        }
        if (j.quilt.present) {
            l << "Quilt";
        }
        if (j.forge.present) {
            l << "Forge";
        }
        if (j.neoforge.present) {
            l << "NeoForge";
        }
        return l.join(" / ");
    };
    if (wrongLoader.size() > 3) {
        QStringList names, files;
        for (const auto* m : wrongLoader) {
            names << m->name;
            files << m->path;
        }
        out << problem("loader", trn("%n mod(s) are made for another mod loader", static_cast<int>(wrongLoader.size())),
                       tr("This instance uses %1, so these won't load: %2.").arg(loaderName(loader), names.join(", ")), tr("Turn them off"),
                       "disable", files, true);
    } else {
        for (const auto* m : wrongLoader) {
            out << problem("loader", tr("%1 is a %2 mod").arg(m->name, madeFor(m->jar)),
                           tr("This instance uses %1, so it won't load.").arg(loaderName(loader)), tr("Turn it off"), "disable",
                           QStringList{ m->path }, true);
        }
    }

    // 2. The same mod twice
    QHash<QString, QList<const ModFile*>> byId;
    for (const auto& m : mods) {
        if (!m.incompatible && m.active()->parsed) {
            for (const auto& id : m.active()->ids) {
                byId[id] << &m;
            }
        }
    }
    QSet<QString> reported;
    for (auto it = byId.begin(); it != byId.end(); ++it) {
        auto copies = it.value();
        if (copies.size() < 2) {
            continue;
        }
        QStringList key;
        for (const auto* c : copies) {
            key << c->path;
        }
        key.sort();
        if (reported.contains(key.join('\n'))) {
            continue;
        }
        reported.insert(key.join('\n'));
        // newest last: by version, then by file date
        std::sort(copies.begin(), copies.end(), [](const ModFile* a, const ModFile* b) {
            const QString va = a->active()->version, vb = b->active()->version;
            if (!va.isEmpty() && !vb.isEmpty() && va != vb) {
                return Version(va) < Version(vb);
            }
            return QFileInfo(a->path).lastModified() < QFileInfo(b->path).lastModified();
        });
        QStringList older, files;
        for (qsizetype i = 0; i < copies.size(); ++i) {
            files << QFileInfo(copies[i]->path).fileName();
            if (i + 1 < copies.size()) {
                older << copies[i]->path;
            }
        }
        out << problem("duplicate", trn("%1 is installed %n times", static_cast<int>(copies.size())).arg(copies.last()->name),
                       tr("Files: %1. The game stops when a mod is there twice.").arg(files.join(", ")),
                       older.size() == 1 ? tr("Turn off the older copy") : tr("Turn off the older copies"), "disable", older, true);
    }

    // 3. Required mods that aren't installed
    QSet<QString> available{ "minecraft", "java", "mixinextras" };
    if (fabricLike) {
        available += { "fabricloader", "fabric-loader" };
        if (loader == "quilt") {
            available.insert("quilt_loader");
        }
    } else {
        available += { "forge", "neoforge", "javafml", "lowcodefml", "fml", "mcp", "minecraftforge" };
    }
    bool unsure = false;
    for (const auto& m : mods) {
        if (m.incompatible) {
            continue;
        }
        const auto& j = m.jar;
        const QList<const ModScan::Descriptor*> descs =
            fabricLike ? QList<const ModScan::Descriptor*>{ &j.fabric, &j.quilt } : QList<const ModScan::Descriptor*>{ &j.forge, &j.neoforge };
        for (const auto* d : descs) {
            if (d->present && !d->parsed) {
                unsure = true;  // this mod might provide anything
            }
            for (const auto& id : d->ids) {
                available.insert(id);
            }
            available.unite(d->provides);
        }
        available.unite(j.nestedIds);
    }
    if (!unsure) {
        QMap<QString, QStringList> missing;  // display name -> mods that need it
        QMap<QString, QString> searchFor;
        for (const auto& m : mods) {
            if (m.incompatible || !m.active()->parsed) {
                continue;
            }
            for (const auto& dep : m.active()->depends) {
                if (dep.isEmpty() || dep.contains("${") || available.contains(dep)) {
                    continue;
                }
                QString display = prettyId(dep), search = display;
                if (fabricLike && isFabricApiPart(dep)) {
                    display = loader == "quilt" ? QStringLiteral("Quilted Fabric API") : QStringLiteral("Fabric API");
                    search = display;
                }
                if (!missing[display].contains(m.name)) {
                    missing[display] << m.name;
                }
                searchFor[display] = search;
            }
        }
        for (auto it = missing.begin(); it != missing.end(); ++it) {
            const QStringList& needers = it.value();
            const QString title = needers.size() == 1
                                      ? tr("%1 needs %2").arg(needers.first(), it.key())
                                      : trn("%1 and %n other mod(s) need %2", static_cast<int>(needers.size() - 1)).arg(needers.first(), it.key());
            out << problem("missing", title, tr("%1 isn't installed, so the game will stop while loading.").arg(it.key()),
                           tr("Install %1").arg(it.key()), "find", searchFor.value(it.key()), false);
        }
    }

    // 4. Mods made for a different Minecraft version (only when the mod states an exact range)
    if (release) {
        QList<QPair<const ModFile*, QString>> wrongMc;
        for (const auto& m : mods) {
            if (m.incompatible || !m.active()->parsed) {
                continue;
            }
            const auto* d = m.active();
            if (fabricLike && !d->mcPredicate.isNull() && !d->mcPredicate.isUndefined()) {
                if (ModScan::matchFabric(d->mcPredicate, mc) == 0) {
                    wrongMc << qMakePair(&m, ModScan::describeFabric(d->mcPredicate));
                }
            } else if (!fabricLike && !d->mcRange.isEmpty()) {
                if (ModScan::matchMaven(d->mcRange, mc) == 0) {
                    wrongMc << qMakePair(&m, ModScan::describeMaven(d->mcRange));
                }
            }
        }
        if (wrongMc.size() > 3) {
            QStringList names;
            for (const auto& [m, range] : wrongMc) {
                names << m->name;
            }
            out << problem("minecraft", trn("%n mod(s) are made for another Minecraft version", static_cast<int>(wrongMc.size())),
                           tr("This instance runs Minecraft %1. These won't load: %2.").arg(mc, names.join(", ")),
                           tr("Find versions for %1").arg(mc), "find", QString(), false);
        } else {
            for (const auto& [m, range] : wrongMc) {
                out << problem("minecraft", tr("%1 is made for Minecraft %2").arg(m->name, range),
                               tr("This instance runs Minecraft %1, so it won't load.").arg(mc), tr("Find a version for %1").arg(mc), "find",
                               m->name, false);
            }
        }
    }
}

}  // namespace

QVariantList run(MinecraftInstance* inst)
{
    QVariantList out;
    if (!inst) {
        return out;
    }
    QElapsedTimer timer;
    timer.start();
    auto* profile = inst->getPackProfile();
    const QString mc = profile->getComponentVersion("net.minecraft");
    QString loader;
    static const QList<QPair<QString, QString>> loaders = { { "neoforge", "net.neoforged" },
                                                            { "forge", "net.minecraftforge" },
                                                            { "quilt", "org.quiltmc.quilt-loader" },
                                                            { "fabric", "net.fabricmc.fabric-loader" } };
    for (const auto& [key, uid] : loaders) {
        if (!profile->getComponentVersion(uid).isEmpty()) {
            loader = key;
            break;
        }
    }
    checkJava(inst, mc, out);
    if (!loader.isEmpty()) {
        checkMods(inst, loader, mc, out);
    }
    qInfo() << "[tools] pre-launch check of" << inst->id() << "found" << out.size() << "problem(s) in" << timer.elapsed() << "ms";
    return out;
}

}  // namespace LaunchCheck
