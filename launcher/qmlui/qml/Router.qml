pragma Singleton
import QtQuick

// App-wide navigation. Pages call Router.go()/open()/back(); Main.qml performs them.
//   go(page)            switch top-level section (sidebar): "home" | "instances" | "browse" | "appearance" | "settings"
//   open(page, params)  push a sub-page, e.g. open("instance", { instanceId, tab: "mods" }) or open("create")
QtObject {
    property string section: "home"
    property string selectedId: ""        // instance shown on Home
    signal goRequested(string page)
    signal openRequested(string page, var params)
    signal backRequested()
    signal toastRequested(string message, bool isError)
    signal accountsRequested()

    function go(page) { goRequested(page) }
    function open(page, params) { openRequested(page, params || {}) }
    function back() { backRequested() }
    function toast(message, isError) { toastRequested(message, !!isError) }
    function openAccounts() { accountsRequested() }
    function openInstance(id, tab) { open("instance", { instanceId: id, tab: tab || "overview" }) }
}
