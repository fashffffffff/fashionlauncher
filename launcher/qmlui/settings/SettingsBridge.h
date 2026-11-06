// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Launcher-wide and per-instance settings (Java, memory, behaviour, language).
 * Exposed to QML as "Settings".
 */
class SettingsBridge : public QObject {
    Q_OBJECT

   public:
    explicit SettingsBridge(QObject* parent = nullptr);
};
