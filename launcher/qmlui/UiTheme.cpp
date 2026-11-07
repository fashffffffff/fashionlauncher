// SPDX-License-Identifier: GPL-3.0-only
#include "UiTheme.h"

#include <QDir>
#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QColor>
#include <QImage>
#include <QImageReader>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "settings/SettingsObject.h"

namespace {
const QString kBuiltinPrefix = QStringLiteral("builtin:");
const QString kUserPrefix = QStringLiteral("user:");
const QString kBuiltinDir = QStringLiteral(":/qmlui/themes");
const QString kDefaultTheme = QStringLiteral("builtin:fashion");
const QString kPersonalFile = QStringLiteral("_personal.json");
const QStringList kLayoutSections = { "shape", "type", "layout", "motion" };

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

    m_personal = readTheme(FS::PathCombine(m_userDir, kPersonalFile));

    reloadThemes();
    apply(APPLICATION->settings()->get("QmlUiTheme").toString());
}

bool UiTheme::keepLayout() const
{
    return APPLICATION->settings()->get("QmlThemeKeepLayout").toBool();
}

bool UiTheme::keepBackground() const
{
    return APPLICATION->settings()->get("QmlThemeKeepBackground").toBool();
}

bool UiTheme::isPersonal(const QString& group) const
{
    return (keepLayout() && kLayoutSections.contains(group)) || (keepBackground() && group == "background");
}

void UiTheme::setKeepLayout(bool keep)
{
    if (keep == keepLayout()) {
        return;
    }
    if (keep) {
        // Start from what is on screen now, so turning this on changes nothing visible.
        for (const auto& sec : kLayoutSections) {
            m_personal[sec] = m_merged.value(sec);
        }
        savePersonal();
    }
    APPLICATION->settings()->set("QmlThemeKeepLayout", keep);
    rebuild();
}

void UiTheme::setKeepBackground(bool keep)
{
    if (keep == keepBackground()) {
        return;
    }
    if (keep) {
        m_personal["background"] = m_merged.value("background");
        savePersonal();
    }
    APPLICATION->settings()->set("QmlThemeKeepBackground", keep);
    rebuild();
}

void UiTheme::savePersonal()
{
    m_selfWrite = true;
    QFile f(FS::PathCombine(m_userDir, kPersonalFile));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(m_personal).toJson(QJsonDocument::Indented));
    }
    m_selfWrite = false;
}

QVariantMap UiTheme::colors() const
{
    auto map = section("colors");
    if (accentFromBackground() && !m_imageAccent.isEmpty()) {
        map["accent"] = m_imageAccent;
        map["onAccent"] = m_imageOnAccent;
    }
    return map;
}

bool UiTheme::accentFromBackground() const
{
    return APPLICATION->settings()->get("QmlAccentFromBackground").toBool();
}

void UiTheme::setAccentFromBackground(bool on)
{
    APPLICATION->settings()->set("QmlAccentFromBackground", on);
    setAccentImage(m_accentImage);
    emit changed();
}

namespace {
double luminance(const QColor& c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}
}  // namespace

void UiTheme::setAccentImage(const QString& url)
{
    m_accentImage = url;
    if (!accentFromBackground()) {
        return;
    }
    QString path = url;
    if (path.startsWith("qrc:")) {
        path = path.mid(3);  // "qrc:/x" -> ":/x"
    } else if (path.startsWith("file:")) {
        path = QUrl(path).toLocalFile();
    }
    QImageReader reader(path);
    reader.setScaledSize(QSize(64, 36));
    const QImage img = reader.read();
    QString accent, onAccent;
    if (!img.isNull()) {
        // Hue histogram weighted by colourfulness: the most present vivid hue wins, greys don't count.
        double weight[12] = {};
        double r[12] = {}, g[12] = {}, b[12] = {};
        for (int y = 0; y < img.height(); y++) {
            for (int x = 0; x < img.width(); x++) {
                const QColor c = img.pixelColor(x, y);
                const double sat = c.hsvSaturationF(), val = c.valueF();
                if (sat < 0.25 || val < 0.2) {
                    continue;
                }
                const int bin = qBound(0, int(c.hsvHueF() * 12), 11);
                const double w = sat * val;
                weight[bin] += w;
                r[bin] += c.redF() * w;
                g[bin] += c.greenF() * w;
                b[bin] += c.blueF() * w;
            }
        }
        int best = 0;
        for (int i = 1; i < 12; i++) {
            if (weight[i] > weight[best]) {
                best = i;
            }
        }
        if (weight[best] > 0) {
            QColor c = QColor::fromRgbF(r[best] / weight[best], g[best] / weight[best], b[best] / weight[best]);
            // Make it usable as an accent: vivid enough, bright on dark themes, deeper on light ones.
            const double h = c.hsvHueF();
            c = QColor::fromHsvF(h < 0 ? 0 : h, qMax(c.hsvSaturationF(), 0.55), dark() ? 0.92 : 0.62);
            accent = c.name();
            onAccent = luminance(c) > 0.35 ? "#15171C" : "#FFFFFF";
        }
    }
    if (accent != m_imageAccent) {
        m_imageAccent = accent;
        m_imageOnAccent = onAccent;
        emit changed();
    }
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
            if (info.baseName() == "base" || info.baseName().startsWith('_')) {
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
    for (const auto& sec : kLayoutSections + QStringList{ "background" }) {
        if (isPersonal(sec)) {
            m_merged[sec] = deepMerge(m_base.value(sec).toObject(), m_personal.value(sec).toObject());
        }
    }
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
    if (isPersonal(group)) {
        // Kept across themes: write to the personal file, leave the theme alone
        auto obj = m_personal.value(group).toObject();
        obj[key] = QJsonValue::fromVariant(value);
        m_personal[group] = obj;
        savePersonal();
        rebuild();
        return;
    }
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
    if (keepBackground()) {
        const QString target = FS::PathCombine(m_userDir, "_personal-bg." + QFileInfo(source).suffix());
        QFile::remove(target);
        QFile::copy(source, target);
        setToken("background", "image", QFileInfo(target).fileName());
        setToken("background", "mode", "image");
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
