// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Installed mods/resource packs/shaders of an instance, and browsing/installing from Modrinth/CurseForge.
 * Exposed to QML as "Resources".
 */
class ResourcesBridge : public QObject {
    Q_OBJECT

   public:
    explicit ResourcesBridge(QObject* parent = nullptr);
};
