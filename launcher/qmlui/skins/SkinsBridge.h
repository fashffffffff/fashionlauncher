// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Skins: 3D preview, local skin library, upload/cape for Microsoft accounts.
 * Exposed to QML as "Skins". Register any settings this feature needs in the constructor
 * (APPLICATION->settings()->registerSetting), not in Application.cpp.
 */
class SkinsBridge : public QObject {
    Q_OBJECT

   public:
    explicit SkinsBridge(QObject* parent = nullptr);
};
