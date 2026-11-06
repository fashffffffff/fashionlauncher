// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QVariantList>

#include "java/JavaMetadata.h"
#include "tasks/Task.h"

/**
 * Download a Java runtime from the meta server, for the QML Java dialog (`JavaInstaller` in QML).
 * Replaces the classic Java::InstallDialog: pick a provider, a major version, then a build.
 *
 * `majors`: [{ id ("java17"), label ("Java 17"), recommended }]; `builds`: [{ index, name, version, vendor, type, date }].
 * Downloads are registered in TaskCenter, so the global indicator shows them; `installFinished(ok, message)` follows.
 */
class JavaInstaller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList majors READ majors NOTIFY majorsChanged)
    Q_PROPERTY(QVariantList builds READ builds NOTIFY buildsChanged)
    /// "" | "majors" | "builds"
    Q_PROPERTY(QString loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadingChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY installingChanged)

   public:
    explicit JavaInstaller(QObject* parent = nullptr);

    QVariantList majors() const { return m_majors; }
    QVariantList builds() const { return m_builds; }
    QString loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool installing() const { return m_installing > 0; }

    /// [{ uid, name }]
    Q_INVOKABLE QVariantList providers() const;
    /// Load the major versions a provider offers; recommended ones are those the instance's Minecraft wants
    /// (or every version Minecraft uses when instanceId is empty).
    Q_INVOKABLE void loadMajors(const QString& uid, const QString& instanceId = QString());
    /// Load the builds of one major version for this computer
    Q_INVOKABLE void loadBuilds(const QString& majorId);
    /// Download and install builds[index]. Returns false if it can't start.
    Q_INVOKABLE bool install(int index);

   signals:
    void majorsChanged();
    void buildsChanged();
    void loadingChanged();
    void installingChanged();
    void installFinished(bool ok, const QString& message);

   private:
    void setLoading(const QString& what, const QString& error = QString());
    void fillMajors();
    void fillBuilds();
    QStringList recommendedMajors() const;

    QString m_uid;
    QString m_instanceId;
    QString m_majorId;
    QVariantList m_majors;
    QVariantList m_builds;
    QList<Java::MetadataPtr> m_buildList;
    QString m_loading;
    QString m_error;
    int m_installing = 0;
    int m_generation = 0;  // ignores late answers after the selection changed
    Task::Ptr m_loadTask;
};
