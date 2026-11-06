// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Serverless friends and play-together: talks to the Rust networking sidecar (DHT rendezvous, STUN, hole punching, QUIC tunnel).
 * Exposed to QML as "Friends". Register any settings this feature needs in the constructor
 * (APPLICATION->settings()->registerSetting), not in Application.cpp.
 */
class FriendsBridge : public QObject {
    Q_OBJECT

   public:
    explicit FriendsBridge(QObject* parent = nullptr);
};
