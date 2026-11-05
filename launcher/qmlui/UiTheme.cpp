// SPDX-License-Identifier: GPL-3.0-only
#include "UiTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "settings/SettingsObject.h"

namespace {
const QString kBuiltinPrefix = QStringLiteral("builtin:");
const QString kUserPrefix = QStringLiteral("user:");
const QString kBuiltinDir = QStringLiteral(":/qmlui/themes");
const QString kDefaultTheme = QStringLiteral("builtin:deepslate");

QJsonObject deepMerge(QJsonObject base, const QJsonObject& over)
{
    for (auto it = over.begin(); it != over.end(); ++it) {
        if (it->isObject() && base.value(it.key()).isObject()) {
            base[it.key()] = deepMerge(base.value(it.key()).toObject(), it->toObject());
        } else {
            base[it.key()] = *it;
        }
    }
    return base;
}
}  // namespace

UiTheme::UiTheme(QObject* parent) : QObject(parent)
{
    m_userDir = FS::PathCombine(APPLICATION->dataRoot(), "ui-themes");
    QDir().mkpath(m_userDir);

    m_base = readTheme(kBuiltinDir + "/base.json");

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &UiTheme::saveNow);

    // Live reload: editing the active theme file in any text editor updates the launcher immediately.
    m_watcher.addPath(m_userDir);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { reloadThemes(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (m_selfWrite) {
            return;
        }
        if (!m_builtin && QFileInfo(path) == QFileInfo(pathForId(m_id))) {
            apply(m_id);
        }
    });

    reloadThemes();
    apply(APPLICATION->settings()->get("QmlUiTheme").toString());
}

QString UiTheme::name() const
{
    return m_merged.value("name").toString();
}

bool UiTheme::dark() const
{
    return m_merged.value("dark").toBool(true);
}

QVariantMap UiTheme::section(const QString& key) const
{
    return m_merged.value(key).toObject().toVariantMap();
}

QJsonObject UiTheme::readTheme(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = tr("Can't open %1").arg(path);
        }
        return {};
    }
    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = tr("%1 has a JSON error at character %2: %3")
                         .arg(QFileInfo(path).fileName())
                         .arg(parseError.offset)
                         .arg(parseError.errorString());
        }
        return {};
    }
    return doc.object();
}

QString UiTheme::pathForId(const QString& id) const
{
    if (id.startsWith(kBuiltinPrefix)) {
        return kBuiltinDir + "/" + id.mid(kBuiltinPrefix.size()) + ".json";
    }
    if (id.startsWith(kUserPrefix)) {
        return FS::PathCombine(m_userDir, id.mid(kUserPrefix.size()) + ".json");
    }
    return {};
}

void UiTheme::reloadThemes()
{
    QVariantList list;
    auto addDir = [&](const QString& dir, const QString& prefix) {
        const auto entries = QDir(dir).entryInfoList({ "*.json" }, QDir::Files, QDir::Name);
        for (const auto& info : entries) {
            if (info.baseName() == "base") {
                continue;
            }
            auto obj = deepMerge(m_base, readTheme(info.absoluteFilePath()));
            auto colors = obj.value("colors").toObject();
            list.append(QVariantMap{
                { "id", prefix + info.baseName() },
                { "name", obj.value("name").toString(info.baseName()) },
                { "author", obj.value("author").toString() },
                { "builtin", prefix == kBuiltinPrefix },
                { "bg", colors.value("bg").toString() },
                { "surface", colors.value("surface").toString() },
                { "text", colors.value("text").toString() },
                { "accent", colors.value("accent").toString() },
            });
        }
    };
    addDir(kBuiltinDir, kBuiltinPrefix);
    addDir(m_userDir, kUserPrefix);
    m_themes = list;

    // keep watching every user theme file for external edits
    if (!m_watcher.files().isEmpty()) {
        m_watcher.removePaths(m_watcher.files());
    }
    for (const auto& info : QDir(m_userDir).entryInfoList({ "*.json" }, QDir::Files)) {
        m_watcher.addPath(info.absoluteFilePath());
    }
    emit themesChanged();
}

void UiTheme::apply(const QString& requestedId)
{
    QString id = requestedId.isEmpty() ? kDefaultTheme : requestedId;
    QString error;
    auto theme = readTheme(pathForId(id), &error);
    if (theme.isEmpty()) {
        // A broken user theme must never leave the launcher unusable: fall back, but keep the error visible.
        m_lastError = error;
        id = kDefaultTheme;
        theme = readTheme(pathForId(id));
    } else {
        m_lastError.clear();
    }
    m_id = id;
    m_builtin = id.startsWith(kBuiltinPrefix);
    m_theme = theme;
    APPLICATION->settings()->set("QmlUiTheme", m_id);
    rebuild();
}

void UiTheme::rebuild()
{
    m_merged = deepMerge(m_base, m_theme);
    emit changed();
}

QString UiTheme::uniqueUserId(const QString& name) const
{
    QString slug = name.toLower();
    slug.replace(QRegularExpression("[^a-z0-9а-яё]+"), "-");
    slug.remove(QRegularExpression("^-+|-+$"));
    if (slug.isEmpty()) {
        slug = "theme";
    }
    QString candidate = slug;
    for (int i = 2; QFile::exists(FS::PathCombine(m_userDir, candidate + ".json")); i++) {
        candidate = QString("%1-%2").arg(slug).arg(i);
    }
    return kUserPrefix + candidate;
}

QString UiTheme::forkIfBuiltin()
{
    if (!m_builtin) {
        return m_id;
    }
    return duplicate(tr("My %1").arg(name()));
}

QString UiTheme::duplicate(const QString& newName)
{
    QJsonObject copy = m_theme;
    copy["name"] = newName;
    copy.remove("author");
    m_id = uniqueUserId(newName);
    m_builtin = false;
    m_theme = copy;
    APPLICATION->settings()->set("QmlUiTheme", m_id);
    saveNow();
    reloadThemes();
    rebuild();
    return m_id;
}

void UiTheme::setToken(const QString& group, const QString& key, const QVariant& value)
{
    forkIfBuiltin();
    auto obj = m_theme.value(group).toObject();
    obj[key] = QJsonValue::fromVariant(value);
    m_theme[group] = obj;
    rebuild();
    scheduleSave();
}

void UiTheme::rename(const QString& newName)
{
    if (newName.trimmed().isEmpty()) {
        return;
    }
    forkIfBuiltin();
    m_theme["name"] = newName.trimmed();
    rebuild();
    scheduleSave();
}

bool UiTheme::remove(const QString& id)
{
    if (!id.startsWith(kUserPrefix)) {
        return false;
    }
    const bool ok = QFile::remove(pathForId(id));
    if (id == m_id) {
        apply(kDefaultTheme);
    }
    reloadThemes();
    return ok;
}

void UiTheme::setBackgroundImage(const QUrl& file)
{
    const QString source = file.toLocalFile();
    if (source.isEmpty()) {
        return;
    }
    forkIfBuiltin();
    // Copy next to the theme so the theme folder stays self-contained and shareable.
    const QString target = FS::PathCombine(m_userDir, m_id.mid(kUserPrefix.size()) + "-bg." + QFileInfo(source).suffix());
    QFile::remove(target);
    QFile::copy(source, target);
    auto bg = m_theme.value("background").toObject();
    bg["image"] = QFileInfo(target).fileName();
    bg["mode"] = "image";
    m_theme["background"] = bg;
    rebuild();
    scheduleSave();
}

void UiTheme::openThemesFolder() const
{
    DesktopServices::openPath(m_userDir, true);
}

void UiTheme::scheduleSave()
{
    m_saveTimer.start();
}

void UiTheme::saveNow()
{
    if (m_builtin) {
        return;
    }
    m_selfWrite = true;
    QFile f(pathForId(m_id));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(m_theme).toJson(QJsonDocument::Indented));
    }
    f.close();
    m_selfWrite = false;
    // QFileSystemWatcher drops files that get replaced; re-add after our own write.
    m_watcher.addPath(f.fileName());
}
