// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Creating instances: vanilla versions with mod loaders, modpacks from Modrinth/CurseForge, imports.
 * Exposed to QML as "Creator".
 */
class CreateBridge : public QObject {
    Q_OBJECT

   public:
    explicit CreateBridge(QObject* parent = nullptr);
};
