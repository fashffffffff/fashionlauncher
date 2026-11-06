// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Instance sharing: pack manifests (mods by platform version id, side info), share links/files, applying a manifest to a new or existing instance with server/client filters and exclusions.
 * Exposed to QML as "Sharing". Register any settings this feature needs in the constructor
 * (APPLICATION->settings()->registerSetting), not in Application.cpp.
 */
class SharingBridge : public QObject {
    Q_OBJECT

   public:
    explicit SharingBridge(QObject* parent = nullptr);
};
