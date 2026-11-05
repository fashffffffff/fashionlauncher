// SPDX-License-Identifier: GPL-3.0-only
#include "AccountsModel.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>

#include "Application.h"
#include "DesktopServices.h"
#include "minecraft/auth/AccountList.h"

AccountsModel::AccountsModel(QObject* parent) : QIdentityProxyModel(parent)
{
    setSourceModel(APPLICATION->accounts());

    auto emitCount = [this] {
        emit countChanged();
        emit currentChanged();
    };
    connect(this, &QAbstractItemModel::rowsInserted, this, emitCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, emitCount);
    connect(this, &QAbstractItemModel::modelReset, this, emitCount);
    connect(list(), &AccountList::defaultAccountChanged, this, [this] {
        emit currentChanged();
        if (rowCount() > 0) {
            emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { IsDefaultRole });
        }
    });
    connect(list(), &AccountList::listChanged, this, [this] {
        // skins/states may have changed: bust the face image cache
        m_faceRevision++;
        emit currentChanged();
        if (rowCount() > 0) {
            emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
        }
    });
}

AccountList* AccountsModel::list() const
{
    return APPLICATION->accounts();
}

QHash<int, QByteArray> AccountsModel::roleNames() const
{
    return {
        { IdRole, "accountId" }, { NameRole, "name" },   { KindRole, "kind" },   { FaceRole, "face" },
        { IsDefaultRole, "isDefault" }, { StateRole, "state" }, { ErrorRole, "error" },
    };
}

QString AccountsModel::faceUrl(const MinecraftAccountPtr& account) const
{
    return QString("image://accountface/%1?r=%2").arg(account->internalId()).arg(m_faceRevision);
}

QVariant AccountsModel::data(const QModelIndex& index, int role) const
{
    if (role < IdRole) {
        return QIdentityProxyModel::data(index, role);
    }
    if (!index.isValid() || index.row() >= list()->count()) {
        return {};
    }
    const auto account = list()->at(index.row());
    switch (role) {
        case IdRole:
            return account->internalId();
        case NameRole:
            return account->profileName();
        case KindRole:
            return account->typeString();
        case FaceRole:
            return faceUrl(account);
        case IsDefaultRole:
            return list()->defaultAccount() == account;
        case StateRole:
            switch (account->accountState()) {
                case AccountState::Working:
                    return "working";
                case AccountState::Online:
                    return "online";
                case AccountState::Errored:
                case AccountState::Expired:
                case AccountState::Gone:
                case AccountState::Disabled:
                    return "error";
                default:
                    return "ready";
            }
        case ErrorRole:
            return account->lastError();
        default:
            return {};
    }
}

QVariantMap AccountsModel::current() const
{
    const auto account = list()->defaultAccount();
    if (!account) {
        return {};
    }
    return {
        { "accountId", account->internalId() },
        { "name", account->profileName() },
        { "kind", account->typeString() },
        { "face", faceUrl(account) },
    };
}

int AccountsModel::rowOf(const QString& id) const
{
    for (int i = 0; i < list()->count(); i++) {
        if (list()->at(i)->internalId() == id) {
            return i;
        }
    }
    return -1;
}

QString AccountsModel::addOffline(const QString& rawName)
{
    const QString name = rawName.trimmed();
    // Same rule the game uses for player names
    static const QRegularExpression valid("^[A-Za-z0-9_]{3,16}$");
    if (!valid.match(name).hasMatch()) {
        return tr("Use 3–16 characters: Latin letters, digits and underscores.");
    }
    for (int i = 0; i < list()->count(); i++) {
        const auto existing = list()->at(i);
        if (existing->accountType() == AccountType::Offline && existing->profileName().compare(name, Qt::CaseInsensitive) == 0) {
            return tr("You already have an offline account named %1.").arg(name);
        }
    }
    const auto account = MinecraftAccount::createOffline(name);
    if (!account) {
        return tr("Couldn't create the account.");
    }
    account->login()->start();  // completes synchronously for offline accounts
    list()->addAccount(account);
    list()->setDefaultAccount(account);
    return {};
}

void AccountsModel::setDefault(const QString& id)
{
    const int row = rowOf(id);
    if (row >= 0) {
        list()->setDefaultAccount(list()->at(row));
    }
}

void AccountsModel::remove(const QString& id)
{
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    const bool wasDefault = list()->defaultAccount() == list()->at(row);
    list()->removeAccount(list()->index(row));
    if (wasDefault && list()->count() > 0) {
        list()->setDefaultAccount(list()->at(0));
    }
}

void AccountsModel::refresh(const QString& id)
{
    list()->requestRefresh(id);
}

void AccountsModel::startMicrosoftSignIn()
{
    if (m_flow) {
        return;
    }
    m_pending = MinecraftAccount::createBlankMSA();
    m_error.clear();
    m_browserUrl.clear();
    m_deviceCode.clear();
    m_deviceUrl.clear();
    m_status = tr("Contacting Microsoft…");

    // Two flows race each other, as in the classic dialog: a browser redirect and a device code.
    // Whichever finishes first wins.
    m_flow.reset(new AuthFlow(m_pending->accountData(), AuthFlow::Action::Login));
    m_deviceFlow.reset(new AuthFlow(m_pending->accountData(), AuthFlow::Action::DeviceCode));

    for (const auto& flow : { m_flow, m_deviceFlow }) {
        connect(flow.get(), &Task::succeeded, this, [this] { finishSignIn(true); });
        connect(flow.get(), &Task::status, this, [this](QString status) {
            m_status = status;
            emit signInChanged();
        });
        connect(flow.get(), &AuthFlow::authorizeWithBrowser, this, [this](const QUrl& url) {
            m_browserUrl = url.toString();
            emit signInChanged();
        });
        connect(flow.get(), &AuthFlow::authorizeWithBrowserWithExtra, this, [this](QString url, QString code, int) {
            m_deviceUrl = url;
            m_deviceCode = code;
            emit signInChanged();
        });
    }
    connect(m_flow.get(), &Task::failed, this, [this](QString reason) {
        m_browserUrl.clear();
        if (m_deviceFlow && m_deviceFlow->getState() == Task::State::Failed) {
            m_error = reason;
            finishSignIn(false);
        }
        emit signInChanged();
    });
    connect(m_deviceFlow.get(), &Task::failed, this, [this](QString reason) {
        m_deviceCode.clear();
        m_deviceUrl.clear();
        if (m_flow && m_flow->getState() == Task::State::Failed) {
            m_error = reason;
            finishSignIn(false);
        }
        emit signInChanged();
    });

    QMetaObject::invokeMethod(m_flow.get(), &Task::start, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_deviceFlow.get(), &Task::start, Qt::QueuedConnection);
    emit signInChanged();
}

void AccountsModel::finishSignIn(bool success)
{
    // Detach first: aborting the losing flow must not re-enter here.
    auto flow = m_flow;
    auto deviceFlow = m_deviceFlow;
    m_flow.reset();
    m_deviceFlow.reset();
    for (const auto& f : { flow, deviceFlow }) {
        if (f) {
            f->disconnect(this);
            if (f->isRunning()) {
                f->abort();
            }
        }
    }

    if (success && m_pending) {
        const QString name = m_pending->profileName();
        // Signing in again with the same account replaces the old entry
        const int existing = list()->findAccountByProfileId(m_pending->profileId());
        if (existing >= 0) {
            list()->removeAccount(list()->index(existing));
        }
        list()->addAccount(m_pending);
        list()->setDefaultAccount(m_pending);
        emit signedIn(name);
    }
    m_pending.reset();
    emit signInChanged();
}

void AccountsModel::cancelSignIn()
{
    m_error.clear();
    finishSignIn(false);
}

void AccountsModel::openBrowserSignIn() const
{
    if (!m_browserUrl.isEmpty() && !DesktopServices::openUrl(QUrl(m_browserUrl))) {
        QGuiApplication::clipboard()->setText(m_browserUrl);
    }
}

void AccountsModel::openDeviceSignIn() const
{
    QString url = m_deviceUrl;
    if (url == "https://www.microsoft.com/link" && !m_deviceCode.isEmpty()) {
        url += QString("?otc=%1").arg(m_deviceCode);
    }
    DesktopServices::openUrl(QUrl(url));
}

void AccountsModel::copyDeviceCode() const
{
    QGuiApplication::clipboard()->setText(m_deviceCode);
}
