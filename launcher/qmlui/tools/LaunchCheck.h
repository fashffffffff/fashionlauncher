// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QVariantList>

class MinecraftInstance;

/**
 * Pre-launch checks: only problems that are certain to stop the game or a mod from loading.
 * Each problem is a map:
 *   kind       "loader" | "missing" | "duplicate" | "minecraft" | "java"
 *   title      one plain sentence
 *   detail     a second, quieter line
 *   fixLabel   button text ("Turn off the older copy", "Install Fabric API", …)
 *   fixAction  "disable" (fixArg = list of file paths) | "find" (fixArg = search text) | "settings"
 *   autoFix    true when "Fix" can do it without the user picking anything
 */
namespace LaunchCheck {
QVariantList run(MinecraftInstance* instance);
}
