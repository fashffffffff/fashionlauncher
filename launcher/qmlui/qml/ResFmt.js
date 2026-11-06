.pragma library

// Formatting helpers for the mod manager and browser.

function count(n) {
    if (n === undefined || n === null || n < 0) return ""
    if (n >= 1e9) return (n / 1e9).toFixed(n >= 1e10 ? 0 : 1) + "B"
    if (n >= 1e6) return (n / 1e6).toFixed(n >= 1e7 ? 0 : 1) + "M"
    if (n >= 1e3) return (n / 1e3).toFixed(n >= 1e4 ? 0 : 1) + "K"
    return String(n)
}

// Plural noun for a resource kind, for headings and buttons.
function noun(kind) {
    if (kind === "resourcepacks") return qsTr("resource packs")
    if (kind === "shaderpacks") return qsTr("shaders")
    return qsTr("mods")
}

function findLabel(kind) {
    if (kind === "resourcepacks") return qsTr("Find resource packs")
    if (kind === "shaderpacks") return qsTr("Find shaders")
    return qsTr("Find mods")
}

function emptyTitle(kind) {
    if (kind === "resourcepacks") return qsTr("No resource packs yet")
    if (kind === "shaderpacks") return qsTr("No shaders yet")
    return qsTr("No mods yet")
}

function emptyBody(kind) {
    if (kind === "resourcepacks") return qsTr("Find resource packs on Modrinth and CurseForge, or drop .zip files here.")
    if (kind === "shaderpacks") return qsTr("Find shaders on Modrinth and CurseForge, or drop .zip files here. Shaders need Iris or OptiFine.")
    return qsTr("Find mods on Modrinth and CurseForge, or drop .jar files here.")
}

function searchPlaceholder(kind) {
    if (kind === "resourcepacks") return qsTr("Search installed resource packs")
    if (kind === "shaderpacks") return qsTr("Search installed shaders")
    return qsTr("Search installed mods")
}
