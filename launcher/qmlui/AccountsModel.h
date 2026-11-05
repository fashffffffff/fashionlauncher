// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QIdentityProxyModel>

#include "minecraft/auth/AuthFlow.h"
#include "minecraft/auth/MinecraftAccount.h"

class AccountList;

/**
 * QML-facing account list plus the Microsoft and offline sign-in flows.
 */
class AccountsModel : public QIdentityProxyModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)

    // Microsoft sign-in state
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY signInChanged)
    Q_PROPERTY(QString signInStatus READ signInStatus NOTIFY signInChanged)
    Q_PROPERTY(QString signInError READ signInError NOTIFY signInChanged)
    Q_PROPERTY(QString browserUrl READ browserUrl NOTIFY signInChanged)
    Q_PROPERTY(QString deviceCode READ deviceCode NOTIFY signInChanged)
    Q_PROPERTY(QString deviceUrl READ deviceUrl NOTIFY signInChanged)

   public:
    enum Roles {
        IdRole = Qt::UserRole + 200,
        NameRole,
        KindRole,
        FaceRole,
        IsDefaultRole,
        StateRole,
        ErrorRole,
    };

    explicit AccountsModel(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role) const override;
    int count() const { return rowCount(); }
    QVariantMap current() const;

    bool signingIn() const { return m_flow != nullptr; }
    QString signInStatus() const { return m_status; }
    QString signInError() const { return m_error; }
    QString browserUrl() const { return m_browserUrl; }
    QString deviceCode() const { return m_deviceCode; }
    QString deviceUrl() const { return m_deviceUrl; }

    /// Returns an error message, or an empty string on success
    Q_INVOKABLE QString addOffline(const QString& name);
    Q_INVOKABLE void setDefault(const QString& id);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void refresh(const QString& id);

    Q_INVOKABLE void startMicrosoftSignIn();
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void openBrowserSignIn() const;
    Q_INVOKABLE void openDeviceSignIn() const;
    Q_INVOKABLE void copyDeviceCode() const;

   signals:
    void countChanged();
    void currentChanged();
    void signInChanged();
    void signedIn(const QString& name);

   private:
    AccountList* list() const;
    int rowOf(const QString& id) const;
    void finishSignIn(bool success);
    QString faceUrl(const MinecraftAccountPtr& account) const;

    MinecraftAccountPtr m_pending;
    shared_qobject_ptr<AuthFlow> m_flow;
    shared_qobject_ptr<AuthFlow> m_deviceFlow;
    QString m_status;
    QString m_error;
    QString m_browserUrl;
    QString m_deviceCode;
    QString m_deviceUrl;
    int m_faceRevision = 0;
};
