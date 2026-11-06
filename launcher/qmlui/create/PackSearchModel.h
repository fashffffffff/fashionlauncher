// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>

#include "modplatform/ModIndex.h"
#include "net/NetJob.h"

/**
 * Modpack search results from Modrinth or CurseForge, paged. Uses Prism's ResourceAPI search
 * request and pack parsers; only adds the download count the shared model does not keep.
 */
class PackSearchModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

   public:
    enum Roles { PackIdRole = Qt::UserRole + 1, TitleRole, SummaryRole, DownloadsRole, IconUrlRole, AuthorRole };

    struct Hit {
        ModPlatform::IndexedPack::Ptr pack;
        qint64 downloads = 0;
    };

    explicit PackSearchModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString provider() const { return m_provider; }
    void setProvider(const QString& p);
    bool loading() const { return m_job != nullptr; }
    bool canLoadMore() const { return m_more && !m_job; }
    QString error() const { return m_error; }

    /// Start a new search ("" lists the most popular packs).
    Q_INVOKABLE void search(const QString& term);
    Q_INVOKABLE void loadMore();

    ModPlatform::IndexedPack::Ptr packAt(int row) const;
    qint64 downloadsAt(int row) const;

   signals:
    void providerChanged();
    void countChanged();
    void loadingChanged();
    void errorChanged();

   private:
    void request();

    QString m_provider = "modrinth";
    QString m_term;
    int m_offset = 0;
    bool m_more = false;
    bool m_started = false;
    QString m_error;
    QList<Hit> m_hits;
    NetJob::Ptr m_job;
    int m_generation = 0;
};
