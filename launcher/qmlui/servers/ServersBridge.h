// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Servers of instances (servers.dat), status ping, quick join from Home.
 * Exposed to QML as "Servers". Register any settings this feature needs in the constructor
 * (APPLICATION->settings()->registerSetting), not in Application.cpp.
 */
class ServersBridge : public QObject {
    Q_OBJECT

   public:
    explicit ServersBridge(QObject* parent = nullptr);
};
