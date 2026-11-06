// SPDX-License-Identifier: GPL-3.0-only
#include "ResourceCommon.h"

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"

namespace QmlResources {

MinecraftInstance* findInstance(const QString& id)
{
    if (id.isEmpty()) {
        return nullptr;
    }
    auto* inst = APPLICATION->instances()->getInstanceById(id);
    // Components load lazily; version/loader lookups (and Prism's dependency task) need them.
    if (inst && !inst->getPackProfile()->getComponent("net.minecraft")) {
        if (auto res = inst->getPackProfile()->reload(Net::Mode::Offline); !res) {
            qWarning() << "Failed to load components of" << id << ":" << res.error();
        }
    }
    return inst;
}

ResourceFolderModel* folderFor(MinecraftInstance* inst, const QString& kind)
{
    if (!inst) {
        return nullptr;
    }
    if (kind == "mods") {
        return inst->loaderModList();
    }
    if (kind == "resourcepacks") {
        return inst->resourcePackList();
    }
    if (kind == "shaderpacks") {
        return inst->shaderPackList();
    }
    return nullptr;
}

ModPlatform::ResourceType typeFor(const QString& kind)
{
    if (kind == "resourcepacks") {
        return ModPlatform::ResourceType::ResourcePack;
    }
    if (kind == "shaderpacks") {
        return ModPlatform::ResourceType::ShaderPack;
    }
    return ModPlatform::ResourceType::Mod;
}

QString gameVersion(MinecraftInstance* inst)
{
    return inst ? inst->getPackProfile()->getComponentVersion("net.minecraft") : QString();
}

QString loaderNames(MinecraftInstance* inst)
{
    if (!inst) {
        return {};
    }
    QStringList names;
    for (auto loader : inst->getPackProfile()->getModLoadersList()) {
        names << ModPlatform::getModLoaderAsString(loader);
    }
    for (auto& n : names) {
        if (n == "neoforge") {
            n = "NeoForge";
        } else if (n == "legacyfabric") {
            n = "Legacy Fabric";
        } else if (!n.isEmpty()) {
            n[0] = n[0].toUpper();
        }
    }
    return names.join(", ");
}

bool hasModLoader(MinecraftInstance* inst)
{
    return inst && inst->getPackProfile()->getModLoaders().has_value();
}

}  // namespace QmlResources
