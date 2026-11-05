// SPDX-License-Identifier: GPL-3.0-only
#include "ImageProviders.h"

#include <QIcon>

#include "Application.h"
#include "icons/IconList.h"
#include "minecraft/auth/AccountList.h"

QPixmap InstanceIconProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QSize target = requestedSize.width() > 0 && requestedSize.height() > 0 ? requestedSize : QSize(128, 128);
    QIcon icon = APPLICATION->icons()->getIcon(id);
    if (icon.isNull()) {
        icon = APPLICATION->icons()->getIcon("grass");
    }
    QPixmap pixmap = icon.pixmap(target);
    if (size) {
        *size = pixmap.size();
    }
    return pixmap;
}

QPixmap AccountFaceProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString internalId = id.section('?', 0, 0);
    const int side = qMax(requestedSize.width(), requestedSize.height()) > 0 ? qMax(requestedSize.width(), requestedSize.height()) : 64;
    auto* accounts = APPLICATION->accounts();
    QPixmap pixmap;
    for (int i = 0; i < accounts->count(); i++) {
        if (accounts->at(i)->internalId() == internalId) {
            pixmap = accounts->at(i)->getFace(side, side);
            break;
        }
    }
    if (pixmap.isNull()) {
        pixmap = QPixmap(side, side);
        pixmap.fill(Qt::transparent);
    }
    if (size) {
        *size = pixmap.size();
    }
    return pixmap;
}
