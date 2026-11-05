// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QQuickImageProvider>

/// image://instanceicon/<iconKey>
class InstanceIconProvider : public QQuickImageProvider {
   public:
    InstanceIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;
};

/// image://accountface/<internalId>?r=<revision>
class AccountFaceProvider : public QQuickImageProvider {
   public:
    AccountFaceProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;
};
