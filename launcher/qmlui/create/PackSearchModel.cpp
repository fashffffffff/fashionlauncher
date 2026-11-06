// SPDX-License-Identifier: GPL-3.0-only
#include "PackSearchModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "Json.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlamePackIndex.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/RPCSink.h"

namespace {
constexpr int PageSize = 25;  // both APIs ask for 25 per page
}

PackSearchModel::PackSearchModel(QObject* parent) : QAbstractListModel(parent) {}

int PackSearchModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_hits.size());
}

QHash<int, QByteArray> PackSearchModel::roleNames() const
{
    return { { PackIdRole, "packId" },     { TitleRole, "title" },     { SummaryRole, "summary" },
             { DownloadsRole, "downloads" }, { IconUrlRole, "iconUrl" }, { AuthorRole, "author" } };
}

QVariant PackSearchModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hits.size()) {
        return {};
    }
    const auto& hit = m_hits.at(index.row());
    const auto& p = hit.pack;
    switch (role) {
        case PackIdRole:
            return p->addonId.toString();
        case Qt::DisplayRole:
        case TitleRole:
            return p->name;
        case SummaryRole:
            return p->description;
        case DownloadsRole:
            return static_cast<double>(hit.downloads);
        case IconUrlRole:
            return p->logoUrl;
        case AuthorRole:
            return p->authors.isEmpty() ? QString() : p->authors.first().name;
        default:
            return {};
    }
}

void PackSearchModel::setProvider(const QString& p)
{
    const QString value = p == "curseforge" ? p : QStringLiteral("modrinth");
    if (value == m_provider) {
        return;
    }
    m_provider = value;
    emit providerChanged();
    if (m_started) {
        search(m_term);
    }
}

ModPlatform::IndexedPack::Ptr PackSearchModel::packAt(int row) const
{
    return row >= 0 && row < m_hits.size() ? m_hits.at(row).pack : nullptr;
}

qint64 PackSearchModel::downloadsAt(int row) const
{
    return row >= 0 && row < m_hits.size() ? m_hits.at(row).downloads : 0;
}

void PackSearchModel::search(const QString& term)
{
    m_started = true;
    m_term = term.trimmed();
    m_offset = 0;
    m_more = false;
    m_generation++;
    if (m_job) {
        m_job->abort();
        m_job.reset();
    }
    beginResetModel();
    m_hits.clear();
    endResetModel();
    emit countChanged();
    request();
}

void PackSearchModel::loadMore()
{
    if (canLoadMore()) {
        request();
    }
}

void PackSearchModel::request()
{
    const bool curse = m_provider == "curseforge";
    const ResourceAPI& api = curse ? static_cast<const ResourceAPI&>(FlameAPI::get()) : static_cast<const ResourceAPI&>(ModrinthAPI::get());

    ResourceAPI::SortingMethod sort{};
    if (curse) {
        sort.index = 2;  // Popularity
    } else {
        sort.name = m_term.isEmpty() ? "downloads" : "relevance";
    }
    ResourceAPI::SearchArgs args{};
    args.type = ModPlatform::ResourceType::Modpack;
    args.offset = m_offset;
    if (!m_term.isEmpty()) {
        args.search = QString::fromUtf8(QUrl::toPercentEncoding(m_term));
    }
    args.sorting = sort;
    args.side = ModPlatform::SideType::NoSide;

    // Same request Prism makes, with a parser that also keeps the download count.
    auto spec = api.searchProjects(args);
    Net::RPC::Spec<QList<Hit>> hitSpec{ spec.first, [curse](const QByteArray& response) -> Result<QList<Hit>> {
                                           QList<Hit> out;
                                           TRY_INTO(auto doc, Json::requireDocument(response, "Modpack search")
                                                                  .and_then([](const auto& v) { return Json::requireObject(v); })
                                                                  .and_then([curse](const auto& v) {
                                                                      return Json::requireArray(v, curse ? "data" : "hits");
                                                                  }))
                                           for (const auto& raw : doc) {
                                               const auto obj = raw.toObject();
                                               Hit hit;
                                               hit.pack = std::make_shared<ModPlatform::IndexedPack>();
                                               if (curse) {
                                                   TRY(Flame::Parse::loadIndexedPack(*hit.pack, obj))
                                                   hit.downloads = static_cast<qint64>(obj["downloadCount"].toDouble());
                                               } else {
                                                   TRY(Modrinth::Parse::loadIndexedPack(*hit.pack, obj))
                                                   hit.downloads = static_cast<qint64>(obj["downloads"].toDouble());
                                               }
                                               out << hit;
                                           }
                                           return out;
                                       } };
    auto [action, result] = Net::RPC::make<QList<Hit>>(hitSpec);
    if (!action) {
        return;
    }
    auto job = makeShared<NetJob>(QString("%1::QmlSearch").arg(curse ? "Flame" : "Modrinth"), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(action);

    const int gen = m_generation;
    connect(job.get(), &NetJob::succeeded, this, [this, gen, result] {
        if (gen != m_generation) {
            return;
        }
        const QList<Hit> hits = *result;
        m_more = hits.size() >= PageSize;
        m_offset += PageSize;
        if (!hits.isEmpty()) {
            beginInsertRows({}, static_cast<int>(m_hits.size()), static_cast<int>(m_hits.size() + hits.size() - 1));
            m_hits.append(hits);
            endInsertRows();
            emit countChanged();
        }
    });
    connect(job.get(), &NetJob::failed, this, [this, gen](const QString& reason) {
        if (gen != m_generation) {
            return;
        }
        m_error = reason.isEmpty() ? tr("Could not reach the server.") : reason;
        emit errorChanged();
    });
    connect(job.get(), &NetJob::finished, this, [this, gen] {
        if (gen != m_generation) {
            return;
        }
        m_job.reset();
        emit loadingChanged();
    });

    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
    m_job = job;
    emit loadingChanged();
    job->start();
}
