// SPDX-License-Identifier: GPL-3.0-only
#include "GameLog.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QUrl>

#include "Application.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "launch/LaunchTask.h"
#include "launch/LogModel.h"

GameLog::GameLog(QObject* parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

GameLog::~GameLog() = default;

void GameLog::setInstanceId(const QString& id)
{
    if (id == m_instanceId) {
        return;
    }
    if (m_instance) {
        m_instance->disconnect(this);
    }
    m_instanceId = id;
    m_instance = APPLICATION->instances()->getInstanceById(id);
    emit instanceIdChanged();
    if (!m_instance) {
        useModel(nullptr, "none");
        return;
    }
    // A new launch replaces the log; a finished one keeps its log until then.
    connect(m_instance, &BaseInstance::launchTaskChanged, this, [this](LaunchTask* task) {
        if (task) {
            attach(task);
        }
    });
    if (auto* task = m_instance->getLaunchTask()) {
        attach(task);
    } else {
        useModel(nullptr, "none");
    }
}

void GameLog::attach(LaunchTask* task)
{
    useModel(task->getLogModel(), "live");
}

void GameLog::useModel(shared_qobject_ptr<LogModel> model, const QString& source, const QString& fileName)
{
    if (m_model) {
        m_model->disconnect(this);
    }
    m_model = std::move(model);
    setSourceModel(m_model.get());
    m_source = source;
    m_fileName = fileName;
    emit sourceChanged();
    recount();
    if (m_model) {
        connect(m_model.get(), &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int first, int last) {
            for (int row = first; row <= last; row++) {
                const auto name = levelName(m_model->data(m_model->index(row), LogModel::LevelRole).toInt());
                m_warnings += name == "warn";
                m_errors += name == "error";
            }
            emit countsChanged();
        });
        connect(m_model.get(), &QAbstractItemModel::rowsAboutToBeRemoved, this, [this](const QModelIndex&, int first, int last) {
            for (int row = first; row <= last; row++) {
                const auto name = levelName(m_model->data(m_model->index(row), LogModel::LevelRole).toInt());
                m_warnings -= name == "warn";
                m_errors -= name == "error";
            }
        });
        connect(m_model.get(), &QAbstractItemModel::rowsRemoved, this, &GameLog::countsChanged);
        connect(m_model.get(), &QAbstractItemModel::modelReset, this, &GameLog::recount);
    }
}

void GameLog::recount()
{
    m_warnings = 0;
    m_errors = 0;
    if (m_model) {
        for (int row = 0; row < m_model->rowCount(); row++) {
            const auto name = levelName(m_model->data(m_model->index(row), LogModel::LevelRole).toInt());
            m_warnings += name == "warn";
            m_errors += name == "error";
        }
    }
    emit countsChanged();
}

int GameLog::totalLines() const
{
    return m_model ? m_model->rowCount() : 0;
}

QString GameLog::levelName(int level)
{
    switch (static_cast<MessageLevelValue>(level)) {
        case MessageLevelValue::Launcher:
            return "launcher";
        case MessageLevelValue::Warning:
            return "warn";
        case MessageLevelValue::Error:
        case MessageLevelValue::Fatal:
        case MessageLevelValue::StdErr:
            return "error";
        case MessageLevelValue::Trace:
        case MessageLevelValue::Debug:
            return "debug";
        default:
            return "info";
    }
}

QHash<int, QByteArray> GameLog::roleNames() const
{
    return { { LineRole, "line" }, { LevelNameRole, "level" } };
}

QVariant GameLog::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto src = mapToSource(index);
    if (role == LineRole || role == Qt::DisplayRole) {
        return sourceModel()->data(src, Qt::DisplayRole);
    }
    if (role == LevelNameRole) {
        return levelName(sourceModel()->data(src, LogModel::LevelRole).toInt());
    }
    return {};
}

bool GameLog::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const auto idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const auto level = levelName(sourceModel()->data(idx, LogModel::LevelRole).toInt());
    if ((level == "launcher" && !m_showLauncher) || ((level == "info" || level == "debug") && !m_showInfo) ||
        (level == "warn" && !m_showWarnings) || (level == "error" && !m_showErrors)) {
        return false;
    }
    if (!m_query.isEmpty()) {
        return sourceModel()->data(idx, Qt::DisplayRole).toString().contains(m_query, Qt::CaseInsensitive);
    }
    return true;
}

void GameLog::refilter()
{
    invalidateFilter();
    emit filterChanged();
}

void GameLog::setQuery(const QString& query)
{
    if (query != m_query) {
        m_query = query;
        refilter();
    }
}

void GameLog::setShowLauncher(bool on)
{
    if (on != m_showLauncher) {
        m_showLauncher = on;
        refilter();
    }
}

void GameLog::setShowInfo(bool on)
{
    if (on != m_showInfo) {
        m_showInfo = on;
        refilter();
    }
}

void GameLog::setShowWarnings(bool on)
{
    if (on != m_showWarnings) {
        m_showWarnings = on;
        refilter();
    }
}

void GameLog::setShowErrors(bool on)
{
    if (on != m_showErrors) {
        m_showErrors = on;
        refilter();
    }
}

bool GameLog::loadLastLogFile()
{
    if (!m_instance) {
        return false;
    }
    const QString path = FS::PathCombine(m_instance->gameRoot(), "logs/latest.log");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    auto model = makeShared<LogModel>();
    model->setMaxLines(std::max<int>(1000, lines.size() + 1));
    // "[12:00:00] [Render thread/WARN]: ..."; lines without a level (stack traces) keep the previous one.
    static const QRegularExpression levelRe(R"(^\[[^\]]*\] \[[^\]]*/(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\])");
    MessageLevel level = MessageLevel::Info;
    for (const auto& raw : lines) {
        QString line = raw;
        line.remove('\r');
        if (const auto m = levelRe.match(line); m.hasMatch()) {
            level = MessageLevel::fromName(m.captured(1));
        }
        model->append(level, line);
    }
    useModel(model, "file", QFileInfo(path).fileName());
    return true;
}

QString GameLog::text(bool filteredOnly) const
{
    if (!m_model) {
        return {};
    }
    if (!filteredOnly) {
        return m_model->toPlainText();
    }
    QStringList out;
    for (int row = 0; row < rowCount(); row++) {
        out << data(index(row, 0), LineRole).toString();
    }
    return out.join('\n');
}

void GameLog::copy(bool filteredOnly) const
{
    QGuiApplication::clipboard()->setText(text(filteredOnly));
}

QString GameLog::suggestedFileName() const
{
    const QString name = m_instance ? FS::RemoveInvalidFilenameChars(m_instance->name()) : QString("game");
    return FS::PathCombine(QDir::homePath(), QString("%1-%2.log").arg(name, QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss")));
}

QString GameLog::saveTo(const QString& pathOrUrl) const
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (path.isEmpty()) {
        return tr("No file chosen.");
    }
    if (!FS::write(path, text(false).toUtf8())) {
        return tr("Couldn't save the log to %1.").arg(path);
    }
    return {};
}

void GameLog::clear()
{
    if (m_model) {
        m_model->clear();
    }
}
