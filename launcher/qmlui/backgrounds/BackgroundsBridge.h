// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Background gallery: built-in wallpapers, user images and videos, per-instance backgrounds.
 * Exposed to QML as "Backgrounds".
 */
class BackgroundsBridge : public QObject {
    Q_OBJECT

   public:
    explicit BackgroundsBridge(QObject* parent = nullptr);
};
