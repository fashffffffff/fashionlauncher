// SPDX-License-Identifier: GPL-3.0-only
#include "MetaVersionModel.h"

#include <QLocale>
#include <algorithm>

#include "Application.h"
#include "meta/Index.h"

MetaVersionModel::MetaVersionModel(bool filterTypes, QObject* parent) : QAbstractListModel(parent), m_filterTypes(filterTypes) {}

int MetaVersionModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QHash<int, QByteArray> MetaVersionModel::roleNames() const
{
    return { { VersionRole, "version" }, { TypeRole, "type" }, { DateRole, "date" }, { RecommendedRole, "recommended" } };
}

QVariant MetaVersionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const auto& v = m_rows.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case VersionRole:
            return v->version();
        case TypeRole:
            return v->type();
        case DateRole:
            return v->rawTime() > 0 ? QLocale().toString(v->time().date(), QLocale::ShortFormat) : QString();
        case RecommendedRole:
            return v->isRecommended();
        default:
            return {};
    }
}

void MetaVersionModel::setSearch(const QString& s)
{
    if (s == m_search) {
        return;
    }
    m_search = s;
    emit filtersChanged();
    rebuild();
}

void MetaVersionModel::setShowSnapshots(bool on)
{
    if (on == m_showSnapshots) {
        return;
    }
    m_showSnapshots = on;
    emit filtersChanged();
    rebuild();
}

void MetaVersionModel::setShowOld(bool on)
{
    if (on == m_showOld) {
        return;
    }
    m_showOld = on;
    emit filtersChanged();
    rebuild();
}

QString MetaVersionModel::recommended() const
{
    for (const auto& v : m_rows) {
        if (v->isRecommended()) {
            return v->version();
        }
    }
    return m_rows.isEmpty() ? QString() : m_rows.first()->version();
}

bool MetaVersionModel::accepts(const Meta::Version::Ptr& v) const
{
    if (m_filterTypes) {
        const QString type = v->type();
        bool ok = type == "release";
        ok = ok || (m_showSnapshots && (type.contains("snapshot") || type.contains("experiment")));
        ok = ok || (m_showOld && (type.contains("alpha") || type.contains("beta")));
        if (!ok) {
            return false;
        }
    }
    if (!m_parentFilter.isNull()) {
        const auto& reqs = v->requiredSet();
        auto it = std::ranges::find_if(reqs, [](const Meta::Require& r) { return r.uid == "net.minecraft"; });
        if (it == reqs.end() || it->equalsVersion != m_parentFilter) {
            return false;
        }
    }
    if (!m_search.isEmpty() && !v->version().contains(m_search, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

void MetaVersionModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    if (m_list && !m_forceEmpty) {
        for (const auto& v : m_list->versions()) {
            if (accepts(v)) {
                m_rows.append(v);
            }
        }
        std::ranges::stable_sort(m_rows, [](const Meta::Version::Ptr& a, const Meta::Version::Ptr& b) { return a->rawTime() > b->rawTime(); });
    }
    endResetModel();
    emit countChanged();
}

void MetaVersionModel::setSource(const QString& uid, const QString& parentFilter, bool forceEmpty)
{
    m_parentFilter = parentFilter;
    m_forceEmpty = forceEmpty;
    if (uid != m_uid) {
        for (const auto& c : m_listConnections) {
            disconnect(c);
        }
        m_listConnections.clear();
        m_uid = uid;
        m_list = uid.isEmpty() ? nullptr : APPLICATION->metadataIndex()->get(uid);
        if (m_loadTask) {
            m_loadTask.reset();
            emit loadingChanged();
        }
        if (m_list) {
            auto* l = m_list.get();
            m_listConnections << connect(l, &QAbstractItemModel::modelReset, this, &MetaVersionModel::rebuild)
                              << connect(l, &QAbstractItemModel::rowsInserted, this, &MetaVersionModel::rebuild)
                              << connect(l, &QAbstractItemModel::rowsRemoved, this, &MetaVersionModel::rebuild)
                              << connect(l, &QAbstractItemModel::dataChanged, this, &MetaVersionModel::rebuild);
        }
        emit uidChanged();
        rebuild();
        if (m_list && !m_list->isLoaded()) {
            reload(false);
        }
        return;
    }
    rebuild();
}

void MetaVersionModel::reload(bool force)
{
    if (!m_list) {
        return;
    }
    m_error.clear();
    emit errorChanged();
    auto task = m_list->getLoadTask(force);
    if (!task) {
        return;
    }
    m_loadTask = task;
    const QString uid = m_uid;
    connect(task.get(), &Task::finished, this, [this, uid, t = task.get()] {
        if (uid != m_uid || m_loadTask.get() != t) {
            return;
        }
        if (!t->wasSuccessful()) {
            m_error = t->failReason();
            emit errorChanged();
        }
        m_loadTask.reset();
        emit loadingChanged();
        rebuild();
    });
    emit loadingChanged();
    if (!task->isRunning()) {
        task->start();
    }
}

int MetaVersionModel::indexOf(const QString& version) const
{
    for (int i = 0; i < m_rows.size(); i++) {
        if (m_rows.at(i)->version() == version) {
            return i;
        }
    }
    return -1;
}

BaseVersion::Ptr MetaVersionModel::find(const QString& version) const
{
    if (!m_list) {
        return nullptr;
    }
    for (const auto& v : m_list->versions()) {
        if (v->version() == version) {
            return v;
        }
    }
    return nullptr;
}
