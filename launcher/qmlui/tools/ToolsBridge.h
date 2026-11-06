// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Pre-launch checks, mod changelogs before updates, import from other launchers, recommended settings inside modpacks.
 * Exposed to QML as "Tools". Register any settings this feature needs in the constructor
 * (APPLICATION->settings()->registerSetting), not in Application.cpp.
 */
class ToolsBridge : public QObject {
    Q_OBJECT

   public:
    explicit ToolsBridge(QObject* parent = nullptr);
};
