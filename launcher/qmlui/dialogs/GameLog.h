// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPointer>
#include <QSortFilterProxyModel>

#include "QObjectPtr.h"

class LogModel;
class LaunchTask;
class BaseInstance;

/**
 * The game log of one instance for QML: `GameLog { instanceId: ... }` is itself the list model.
 *
 * While the game runs (or after it ran in this session) it shows the live output of the launch;
 * otherwise it can load the last log file from disk. Rows have `line` and `level`
 * ("launcher" | "info" | "warn" | "error"); `query` and the show* flags filter them.
 */
class GameLog : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString instanceId READ instanceId WRITE setInstanceId NOTIFY instanceIdChanged)
    /// "live" | "file" | "none"
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY sourceChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filterChanged)
    Q_PROPERTY(bool showLauncher MEMBER m_showLauncher WRITE setShowLauncher NOTIFY filterChanged)
    Q_PROPERTY(bool showInfo MEMBER m_showInfo WRITE setShowInfo NOTIFY filterChanged)
    Q_PROPERTY(bool showWarnings MEMBER m_showWarnings WRITE setShowWarnings NOTIFY filterChanged)
    Q_PROPERTY(bool showErrors MEMBER m_showErrors WRITE setShowErrors NOTIFY filterChanged)
    Q_PROPERTY(int totalLines READ totalLines NOTIFY countsChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY countsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY countsChanged)

   public:
    enum Roles { LineRole = Qt::UserRole + 300, LevelNameRole };

    explicit GameLog(QObject* parent = nullptr);
    ~GameLog() override;

    QString instanceId() const { return m_instanceId; }
    void setInstanceId(const QString& id);
    QString source() const { return m_source; }
    QString fileName() const { return m_fileName; }
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    void setShowLauncher(bool on);
    void setShowInfo(bool on);
    void setShowWarnings(bool on);
    void setShowErrors(bool on);
    int totalLines() const;
    int warningCount() const { return m_warnings; }
    int errorCount() const { return m_errors; }

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role) const override;

    /// Load the newest log file from disk (logs/latest.log). Returns false if there is none.
    Q_INVOKABLE bool loadLastLogFile();
    /// The visible lines (after filtering) or the whole log
    Q_INVOKABLE QString text(bool filteredOnly = false) const;
    Q_INVOKABLE void copy(bool filteredOnly = false) const;
    /// Save the whole log to a file (path or file:// URL); returns an error message or ""
    Q_INVOKABLE QString saveTo(const QString& pathOrUrl) const;
    /// Where "Save" proposes to put the log (home folder)
    Q_INVOKABLE QString suggestedFileName() const;
    Q_INVOKABLE void clear();

   signals:
    void instanceIdChanged();
    void sourceChanged();
    void filterChanged();
    void countsChanged();

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

   private:
    static QString levelName(int level);
    void attach(LaunchTask* task);
    void useModel(shared_qobject_ptr<LogModel> model, const QString& source, const QString& fileName = {});
    void recount();
    void refilter();

    QString m_instanceId;
    QPointer<BaseInstance> m_instance;
    shared_qobject_ptr<LogModel> m_model;
    QString m_source = "none";
    QString m_fileName;
    QString m_query;
    bool m_showLauncher = true;
    bool m_showInfo = true;
    bool m_showWarnings = true;
    bool m_showErrors = true;
    int m_warnings = 0;
    int m_errors = 0;
};
