// SPDX-License-Identifier: GPL-3.0-only
#include "Snapshots.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include "ContentStore.h"
#include "FileSystem.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/packwiz/Packwiz.h"

namespace Snapshots {

namespace {
const QList<QPair<QString, QString>> kLoaders = {
    { "net.neoforged", "NeoForge" },          { "net.minecraftforge", "Forge" },        { "net.fabricmc.fabric-loader", "Fabric" },
    { "org.quiltmc.quilt-loader", "Quilt" }, { "com.mumfrey.liteloader", "LiteLoader" },
};

QString enabledName(QString name)
{
    if (name.endsWith(".disabled", Qt::CaseInsensitive)) {
        name.chop(9);
    }
    return name;
}

// The same mod across versions: its platform project if known, else its file name.
QString identity(const QJsonObject& f)
{
    const QString project = f.value("projectId").toString();
    return project.isEmpty() ? enabledName(f.value("name").toString()) : f.value("provider").toString() + ":" + project;
}

QJsonObject withoutTime(QJsonObject o)
{
    o.remove("time");
    o.remove("reason");
    o.remove("file");
    return o;
}
}  // namespace

QList<QPair<QString, QString>> categories(MinecraftInstance* inst)
{
    return { { "mods", inst->modsRoot() }, { "resourcepacks", inst->resourcePacksDir() }, { "shaderpacks", inst->shaderPacksDir() } };
}

QStringList contentDirs(MinecraftInstance* inst)
{
    QStringList out;
    for (const auto& [_, d] : categories(inst)) {
        out << d;
    }
    return out;
}

QString dir(const QString& instanceRoot)
{
    return QDir(instanceRoot).filePath(".fashion/snapshots");
}

QJsonObject build(MinecraftInstance* inst, const QHash<QString, QString>& shaByPath)
{
    QJsonObject m = components(inst);
    m["files"] = files(categories(inst), shaByPath);
    return m;
}

QJsonObject components(MinecraftInstance* inst)
{
    QJsonObject m;
    auto* profile = inst->getPackProfile();
    if (profile->getComponentVersion("net.minecraft").isEmpty()) {
        if (auto res = profile->reload(Net::Mode::Offline); !res) {
            qWarning() << "Snapshots: could not load components:" << res.error();
        }
    }
    m["minecraft"] = profile->getComponentVersion("net.minecraft");
    for (const auto& [uid, label] : kLoaders) {
        const QString v = profile->getComponentVersion(uid);
        if (!v.isEmpty()) {
            m["loader"] = QJsonObject{ { "uid", uid }, { "name", label }, { "version", v } };
            break;
        }
    }
    QJsonArray comps;
    for (int i = 0; i < profile->rowCount(); i++) {
        auto c = profile->getComponent(size_t(i));
        if (!c) {
            continue;
        }
        comps.append(QJsonObject{ { "uid", c->getID() }, { "name", c->getName() }, { "version", c->getVersion() }, { "custom", c->isCustom() }, { "dependencyOnly", c->m_dependencyOnly } });
    }
    m["components"] = comps;
    return m;
}

QJsonObject files(const QList<QPair<QString, QString>>& cats, const QHash<QString, QString>& shaByPath)
{
    QJsonObject files;
    for (const auto& [cat, path] : cats) {
        // Prism's metadata (.index/<slug>.pw.toml) by the file name it describes.
        QHash<QString, QPair<QString, Packwiz::V1::Mod>> meta;
        const QDir indexDir(QDir(path).filePath(".index"));
        for (const auto& fi : indexDir.entryInfoList({ "*.pw.toml" }, QDir::Files)) {
            QString slug = fi.fileName();
            slug.chop(8);
            auto mod = Packwiz::V1::getIndexForMod(indexDir, slug);
            if (!mod.filename.isEmpty()) {
                meta.insert(mod.filename, { fi.fileName(), mod });
            }
        }
        QJsonArray list;
        for (const QString& file : ContentStore::contentFiles(path)) {
            const QString sha = shaByPath.value(file);
            if (sha.isEmpty()) {
                continue;
            }
            const QString name = QFileInfo(file).fileName();
            QJsonObject e{ { "name", name },
                           { "sha256", sha },
                           { "size", double(QFileInfo(file).size()) },
                           { "enabled", !name.endsWith(".disabled", Qt::CaseInsensitive) } };
            if (auto it = meta.constFind(enabledName(name)); it != meta.cend()) {
                const auto& mod = it->second;
                e["title"] = mod.name;
                e["provider"] = mod.provider == ModPlatform::ResourceProvider::MODRINTH ? "modrinth" : "curseforge";
                e["projectId"] = mod.projectId.toString();
                e["fileId"] = mod.fileId.toString();
                e["version"] = mod.versionNumber;
                e["url"] = mod.url.toString();
                e["index"] = it->first;
                QFile f(indexDir.filePath(it->first));
                if (f.open(QIODevice::ReadOnly)) {
                    e["indexText"] = QString::fromUtf8(f.readAll());
                }
            }
            list.append(e);
        }
        files[cat] = list;
    }
    return files;
}

QList<QJsonObject> list(const QString& instanceRoot)
{
    QList<QJsonObject> out;
    const QDir d(dir(instanceRoot));
    const auto names = d.entryList({ "*.json" }, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString& n : names) {
        QJsonObject o = read(d.filePath(n));
        if (!o.isEmpty()) {
            out << o;
        }
    }
    return out;
}

QJsonObject read(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (!o.isEmpty()) {
        o["file"] = file;
    }
    return o;
}

QString write(const QString& instanceRoot, QJsonObject manifest, const QString& reason)
{
    const auto existing = list(instanceRoot);
    if (!existing.isEmpty() && withoutTime(existing.first()) == withoutTime(manifest)) {
        return {};
    }
    const QDateTime now = QDateTime::currentDateTime();
    manifest["time"] = now.toMSecsSinceEpoch();
    if (!reason.isEmpty()) {
        manifest["reason"] = reason;
    }
    const QDir d(dir(instanceRoot));
    QDir().mkpath(d.absolutePath());
    QString name = now.toString("yyyyMMdd-HHmmss") + ".json";
    for (int i = 2; d.exists(name); i++) {
        name = now.toString("yyyyMMdd-HHmmss") + QString("-%1.json").arg(i);
    }
    const QString path = d.filePath(name);
    if (auto res = FS::write(path, QJsonDocument(manifest).toJson(QJsonDocument::Indented)); !res) {
        qWarning() << "Snapshots: could not write" << path << res.error();
        return {};
    }
    const auto all = d.entryList({ "*.json" }, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = kKeep; i < all.size(); i++) {
        QFile::remove(d.filePath(all[i]));
    }
    return path;
}

QSet<QString> referenced(const QStringList& instanceRoots)
{
    QSet<QString> out;
    for (const QString& root : instanceRoots) {
        for (const auto& snap : list(root)) {
            const QJsonObject files = snap.value("files").toObject();
            for (auto it = files.begin(); it != files.end(); ++it) {
                for (const auto& v : it.value().toArray()) {
                    out.insert(v.toObject().value("sha256").toString());
                }
            }
        }
    }
    return out;
}

QVariantMap diff(const QJsonObject& snap, const QJsonObject& now)
{
    QVariantMap out;
    const QJsonObject a = snap.value("files").toObject();
    const QJsonObject b = now.value("files").toObject();
    for (const QString& cat : { QStringLiteral("mods"), QStringLiteral("resourcepacks"), QStringLiteral("shaderpacks") }) {
        QHash<QString, QJsonObject> then, current;
        for (const auto& v : a.value(cat).toArray()) {
            then.insert(identity(v.toObject()), v.toObject());
        }
        for (const auto& v : b.value(cat).toArray()) {
            current.insert(identity(v.toObject()), v.toObject());
        }
        int added = 0, removed = 0, changed = 0;
        for (auto it = then.cbegin(); it != then.cend(); ++it) {
            auto cur = current.constFind(it.key());
            if (cur == current.cend()) {
                added++;  // restoring brings it back
            } else if (cur->value("sha256") != it->value("sha256") || cur->value("enabled") != it->value("enabled")) {
                changed++;
            }
        }
        for (auto it = current.cbegin(); it != current.cend(); ++it) {
            if (!then.contains(it.key())) {
                removed++;
            }
        }
        out[cat] = QVariantMap{ { "added", added }, { "removed", removed }, { "changed", changed } };
    }
    // Only what you chose: game version and loader (helper libraries come and go with them).
    const auto loaderOf = [](const QJsonObject& o) {
        const auto l = o.value("loader").toObject();
        return l.value("uid").toString() + "@" + l.value("version").toString();
    };
    const bool compsDiffer = snap.value("minecraft") != now.value("minecraft") || loaderOf(snap) != loaderOf(now);
    out["componentsChanged"] = compsDiffer;
    return out;
}

}  // namespace Snapshots
