// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Shared content store (hardlinks, hash-addressed, GC) and instance snapshots (manifest of last 5 launches, rollback).
 * Exposed to QML as "Store". Register any settings this feature needs in the constructor
 * (APPLICATION->settings()->registerSetting), not in Application.cpp.
 */
class StoreBridge : public QObject {
    Q_OBJECT

   public:
    explicit StoreBridge(QObject* parent = nullptr);
};
