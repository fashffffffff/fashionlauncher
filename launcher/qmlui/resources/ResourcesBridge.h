// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/**
 * Installed mods/resource packs/shaders of an instance, and browsing/installing from Modrinth/CurseForge.
 * Exposed to QML as "Resources". On creation it registers the QML types of this feature
 * (import FFPrism.Resources: InstalledModel, SearchModel) and the image://resicon provider
 * that serves icons of installed items.
 */
class ResourcesBridge : public QObject {
    Q_OBJECT

   public:
    explicit ResourcesBridge(QObject* parent = nullptr);

    /** Minecraft version, loader names and whether a loader is installed, for an instance id. */
    Q_INVOKABLE QVariantMap instanceInfo(const QString& id) const;
};
