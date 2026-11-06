import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Cmd/Ctrl+K: type to jump to any instance, screen or action.
Popup {
    id: pal
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: parent.height * 0.14
    width: Math.min(parent.width - Style.px(80), Style.px(620))
    height: Math.min(parent.height * 0.7, Style.px(480))
    modal: true
    padding: Style.px(10)
    onOpened: { query.text = ""; query.forceActiveFocus(); list.currentIndex = 0 }
    background: Rectangle { radius: Style.radius + Style.px(4); color: Style.surface; border.width: 1; border.color: Style.line }
    Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.55) }

    function entries() {
        const e = []
        for (let i = 0; i < Instances.count; i++) {
            const inst = Instances.get(i)
            e.push({ label: qsTr("Play %1").arg(inst.name), hint: qsTr("Instance"), icon: "play", run: () => Launcher.launch(inst.instanceId) })
            e.push({ label: qsTr("Open %1").arg(inst.name), hint: qsTr("Instance"), icon: "grid", run: () => Router.openInstance(inst.instanceId) })
            e.push({ label: qsTr("Mods of %1").arg(inst.name), hint: qsTr("Instance"), icon: "cube", run: () => Router.openInstance(inst.instanceId, "mods") })
        }
        e.push({ label: qsTr("Create instance"), hint: qsTr("Action"), icon: "plus", run: () => Router.open("create") })
        e.push({ label: qsTr("Accounts"), hint: qsTr("Action"), icon: "user", run: () => Router.openAccounts() })
        e.push({ label: qsTr("Home"), hint: qsTr("Screen"), icon: "home", run: () => Router.go("home") })
        e.push({ label: qsTr("Instances"), hint: qsTr("Screen"), icon: "grid", run: () => Router.go("instances") })
        e.push({ label: qsTr("Browse mods"), hint: qsTr("Screen"), icon: "compass", run: () => Router.go("browse") })
        e.push({ label: qsTr("Appearance"), hint: qsTr("Screen"), icon: "palette", run: () => Router.go("appearance") })
        e.push({ label: qsTr("Settings"), hint: qsTr("Screen"), icon: "gear", run: () => Router.go("settings") })
        for (const t of Theme.themes)
            e.push({ label: qsTr("Theme: %1").arg(t.name), hint: qsTr("Appearance"), icon: "palette", run: () => Theme.apply(t.id) })
        return e
    }
    // every word must match somewhere, so "pl surv" finds "Play Survival with friends"
    readonly property var results: {
        if (!opened) return []
        const words = query.text.toLowerCase().split(/\s+/).filter(w => w)
        const all = entries()
        return words.length ? all.filter(x => words.every(w => x.label.toLowerCase().indexOf(w) >= 0)).slice(0, 50) : all.slice(0, 50)
    }
    function runAt(i) { if (i >= 0 && i < results.length) { const r = results[i]; close(); r.run() } }

    contentItem: ColumnLayout {
        spacing: Style.px(8)
        Field {
            id: query
            Layout.fillWidth: true
            iconName: "search"
            placeholderText: qsTr("Search instances, screens and actions")
            Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
            Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
            Keys.onReturnPressed: pal.runAt(list.currentIndex)
            Keys.onEnterPressed: pal.runAt(list.currentIndex)
            onTextChanged: list.currentIndex = 0
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: pal.results
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: rowBtn
                width: ListView.view.width - Style.px(8)
                height: Style.px(42)
                hoverEnabled: true
                onClicked: pal.runAt(index)
                onHoveredChanged: if (hovered) list.currentIndex = index
                background: Rectangle {
                    radius: Style.radius - 2
                    color: index === list.currentIndex ? Style.alpha(Style.accent, 0.18) : "transparent"
                }
                contentItem: RowLayout {
                    anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                    spacing: Style.px(12)
                    Icon { name: modelData.icon; size: Style.px(18); color: index === list.currentIndex ? Style.accent : Style.textMuted }
                    Txt { text: modelData.label; Layout.fillWidth: true }
                    Txt { text: modelData.hint; muted: true; font.pixelSize: Style.fontSmall }
                }
            }
            Txt { anchors.centerIn: parent; visible: list.count === 0; text: qsTr("Nothing found"); muted: true }
        }
        Txt { text: qsTr("↑↓ to move, Enter to run, Esc to close"); muted: true; font.pixelSize: Style.fontSmall }
    }
}
