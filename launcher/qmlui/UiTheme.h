// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

/**
 * Theme engine for the QML shell.
 *
 * A theme is a small JSON file. Every key is optional: whatever a theme leaves out
 * is taken from the built-in base theme, so a user theme can be as short as
 * { "name": "Mine", "colors": { "accent": "#7ad" } }.
 *
 * Built-in themes live in qrc:/qmlui/themes, user themes in <data>/ui-themes.
 * Editing a built-in theme from the Appearance screen forks it into the user folder.
 */
class UiTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool builtin READ builtin NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QVariantMap shape READ shape NOTIFY changed)
    Q_PROPERTY(QVariantMap type READ type NOTIFY changed)
    Q_PROPERTY(QVariantMap background READ background NOTIFY changed)
    Q_PROPERTY(QVariantMap motion READ motion NOTIFY changed)
    Q_PROPERTY(QVariantMap layout READ layout NOTIFY changed)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themesChanged)
    Q_PROPERTY(QString userThemesDir READ userThemesDir CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

   public:
    explicit UiTheme(QObject* parent = nullptr);

    QString themeId() const { return m_id; }
    QString name() const;
    bool builtin() const { return m_builtin; }
    bool dark() const;
    QVariantMap colors() const { return section("colors"); }
    QVariantMap shape() const { return section("shape"); }
    QVariantMap type() const { return section("type"); }
    QVariantMap background() const { return section("background"); }
    QVariantMap motion() const { return section("motion"); }
    QVariantMap layout() const { return section("layout"); }
    QVariantList themes() const { return m_themes; }
    QString userThemesDir() const { return m_userDir; }
    QString lastError() const { return m_lastError; }

    /// Switch to a theme by id ("builtin:deepslate" or "user:my-theme")
    Q_INVOKABLE void apply(const QString& id);
    /// Change a single token, e.g. setToken("colors", "accent", "#ff0"). Forks built-in themes.
    Q_INVOKABLE void setToken(const QString& group, const QString& key, const QVariant& value);
    Q_INVOKABLE void rename(const QString& newName);
    /// Copy the current theme into a new user theme and switch to it
    Q_INVOKABLE QString duplicate(const QString& newName);
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE void openThemesFolder() const;
    Q_INVOKABLE void reloadThemes();
    /// Pick a local image file as background; copies it into the user themes folder
    Q_INVOKABLE void setBackgroundImage(const QUrl& file);

   signals:
    void changed();
    void themesChanged();

   private:
    QVariantMap section(const QString& key) const;
    QJsonObject readTheme(const QString& path, QString* error = nullptr) const;
    QString pathForId(const QString& id) const;
    QString forkIfBuiltin();
    QString uniqueUserId(const QString& name) const;
    void scheduleSave();
    void saveNow();
    void rebuild();

    QString m_userDir;
    QJsonObject m_base;     // built-in base theme, always fully specified
    QJsonObject m_theme;    // the theme file as written by the user (sparse)
    QJsonObject m_merged;   // base + theme
    QString m_id;
    bool m_builtin = true;
    QString m_lastError;
    QVariantList m_themes;
    QTimer m_saveTimer;
    QFileSystemWatcher m_watcher;
    bool m_selfWrite = false;
};
