pragma Singleton
import QtQuick

// App-wide navigation. Pages call Router.go()/open()/back(); Main.qml performs them.
//   go(page)            switch top-level section (sidebar): "home" | "instances" | "browse" | "appearance" | "settings"
//   open(page, params)  push a sub-page, e.g. open("instance", { instanceId, tab: "mods" }) or open("create")
QtObject {
    property string section: "home"
    property string selectedId: ""        // instance shown on Home
    property string instancesTab: "grid"  // Instances page: "grid" | "stats"
    signal goRequested(string page)
    signal openRequested(string page, var params)
    signal backRequested()
    signal toastRequested(string message, bool isError)
    signal toastActionRequested(string message, string actionText, var action)
    signal gameOptionsRequested(var params)
    signal accountsRequested()
    signal paletteRequested()

    function go(page) { goRequested(page) }
    function open(page, params) { openRequested(page, params || {}) }
    function back() { backRequested() }
    function toast(message, isError) { toastRequested(message, !!isError) }
    /// A toast with one button (e.g. "Undo") that calls `action`; it stays a little longer.
    function toastAction(message, actionText, action) { toastActionRequested(message, actionText, action) }
    /// Copy game settings between instances: params { source, target } (target: id, "*" = all, "" = ask)
    function copyGameOptions(params) { gameOptionsRequested(params || {}) }
    function openAccounts() { accountsRequested() }
    function openPalette() { paletteRequested() }
    function openInstance(id, tab) { open("instance", { instanceId: id, tab: tab || "overview" }) }
}
