// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Community gallery of themes and backgrounds: index download, preview, one-click install.
 * Exposed to QML as "Gallery". Register any settings this feature needs in the constructor
 * (APPLICATION->settings()->registerSetting), not in Application.cpp.
 */
class GalleryBridge : public QObject {
    Q_OBJECT

   public:
    explicit GalleryBridge(QObject* parent = nullptr);
};
