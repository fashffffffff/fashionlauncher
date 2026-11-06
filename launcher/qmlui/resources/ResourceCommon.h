// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

#include "modplatform/ModIndex.h"

class MinecraftInstance;
class ResourceFolderModel;

/** Small helpers shared by the QML resource models. "kind" is "mods" | "resourcepacks" | "shaderpacks". */
namespace QmlResources {

MinecraftInstance* findInstance(const QString& id);
ResourceFolderModel* folderFor(MinecraftInstance* inst, const QString& kind);
ModPlatform::ResourceType typeFor(const QString& kind);
/** Minecraft version of the instance ("" when unknown). */
QString gameVersion(MinecraftInstance* inst);
/** Readable names of the installed mod loaders ("" when vanilla). */
QString loaderNames(MinecraftInstance* inst);
bool hasModLoader(MinecraftInstance* inst);

}  // namespace QmlResources
