import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The instance as it was at its last launches (up to 5), and a way back to any of them.
Item {
    id: tab
    property string instanceId
    property var instance
    property var snapshots: []
    property bool loading: true
    property var pending: null   // snapshot waiting for confirmation
    readonly property bool running: !!(instance && instance.running)

    function reload() { if (instanceId) Store.loadHistory(instanceId) }
    onInstanceIdChanged: reload()
    Component.onCompleted: reload()

    Connections {
        target: Store
        function onHistoryReady(id, list) {
            if (id !== tab.instanceId) return
            tab.snapshots = list
            tab.loading = false
        }
        function onRevisionChanged() { tab.reload() }
        function onRestoreFinished(id, result) {
            if (id !== tab.instanceId) return
            if (result.missing.length === 0)
                Router.toast(qsTr("Restored. Your worlds and settings were not touched."), false)
            else
                missingDialog.show(result.missing)
        }
    }

    function savedText() {
        const b = Store.savedBytes
        if (b <= 0 || Store.sharedInstances < 2) return qsTr("No identical files to share yet")
        const size = b >= 1e9 ? qsTr("%1 GB").arg((b / 1e9).toFixed(1))
                              : qsTr("%1 MB").arg(Math.max(1, Math.round(b / 1e6)))
        return qsTr("Saved %1 across %n instance(s)", "", Store.sharedInstances).arg(size)
    }

    function whenText(s) {
        const d = new Date(s.time), now = new Date()
        const day = 24 * 3600 * 1000
        const startToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime()
        const time = d.toLocaleTimeString(Qt.locale(), "HH:mm")
        const before = s.reason === "restore"
        if (s.time >= startToday)
            return before ? qsTr("Before restoring, today at %1").arg(time) : qsTr("Launched today at %1").arg(time)
        if (s.time >= startToday - day)
            return before ? qsTr("Before restoring, yesterday at %1").arg(time) : qsTr("Launched yesterday at %1").arg(time)
        const date = d.toLocaleDateString(Qt.locale(), "d MMMM")
        return before ? qsTr("Before restoring, %1 at %2").arg(date).arg(time) : qsTr("Launched %1 at %2").arg(date).arg(time)
    }

    function versionText(s) {
        const mc = s.minecraft ? qsTr("Minecraft %1").arg(s.minecraft) : qsTr("Unknown version")
        return s.loader ? mc + ", " + s.loader + " " + s.loaderVersion : mc
    }

    // What restoring would change, compared with the instance now.
    function diffText(s) {
        const m = s.diff.mods, parts = []
        if (m.added) parts.push(qsTr("+%n mod(s)", "", m.added))
        if (m.removed) parts.push(m.added ? "−" + m.removed : qsTr("−%n mod(s)", "", m.removed))
        if (m.changed) parts.push(qsTr("%n updated", "", m.changed))
        let packs = 0
        for (const k of ["resourcepacks", "shaderpacks"])
            packs += s.diff[k].added + s.diff[k].removed + s.diff[k].changed
        if (packs) parts.push(qsTr("%n pack(s) differ", "", packs))
        if (s.diff.componentsChanged) parts.push(qsTr("other game version"))
        return parts.length ? parts.join(", ") : qsTr("Same as now")
    }

    function sameAsNow(s) { return diffText(s) === qsTr("Same as now") }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(14)

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: head.implicitHeight + Style.px(32)
            radius: Style.radius
            color: Style.panel(1)
            border.width: 1
            border.color: Style.panelBorder
            ColumnLayout {
                id: head
                anchors { fill: parent; margins: Style.px(16) }
                spacing: Style.px(12)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(12)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(2)
                        Txt { text: qsTr("Version history"); font.pixelSize: Style.fontLead; font.weight: Font.DemiBold }
                        Txt {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            muted: true
                            text: qsTr("The last %1 launches are remembered. You can go back to any of them; worlds and settings stay as they are.").arg(5)
                        }
                    }
                    Txt { text: tab.savedText(); color: Store.savedBytes > 0 ? Style.success : Style.textMuted; font.weight: Font.DemiBold }
                }
                SettingsSwitch {
                    label: qsTr("Share identical files between instances")
                    description: qsTr("A mod used in several instances takes disk space once. Deleting it in one instance never affects the others.")
                    checked: Store.shareFiles
                    onToggled: (on) => Store.shareFiles = on
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Style.px(10)
            model: tab.snapshots
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: list.width
                implicitHeight: row.implicitHeight + Style.px(28)
                radius: Style.radius
                color: Style.panel(1)
                border.width: 1
                border.color: Style.panelBorder
                RowLayout {
                    id: row
                    anchors { fill: parent; margins: Style.px(14) }
                    spacing: Style.px(14)
                    Icon { name: modelData.reason === "restore" ? "refresh" : "play"; size: Style.px(20); color: Style.textMuted }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(3)
                        Txt { text: tab.whenText(modelData); font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                        Txt { text: tab.versionText(modelData) + "  ·  " + qsTr("%n mod(s)", "", modelData.mods); muted: true }
                        Txt {
                            text: tab.diffText(modelData)
                            color: tab.sameAsNow(modelData) ? Style.textMuted : Style.text
                            font.pixelSize: Style.fontSmall
                        }
                        Txt {
                            visible: modelData.unavailable > 0
                            text: qsTr("%n file(s) from this launch can't be brought back", "", modelData.unavailable)
                            color: Style.danger
                            font.pixelSize: Style.fontSmall
                        }
                    }
                    QuietButton {
                        text: tab.sameAsNow(modelData) ? qsTr("Current") : qsTr("Restore this version")
                        iconName: tab.sameAsNow(modelData) ? "" : "refresh"
                        enabled: !tab.sameAsNow(modelData) && !tab.running && Store.restoring === ""
                        tip: tab.running ? qsTr("Close the game first") : ""
                        onClicked: { tab.pending = modelData; confirm.open() }
                    }
                }
            }
            Txt {
                anchors.centerIn: parent
                width: Math.min(parent.width, Style.px(440))
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: !tab.loading && tab.snapshots.length === 0
                text: qsTr("Nothing here yet. Each time you play, the mods and game version are remembered so you can come back to them.")
                muted: true
            }
        }
    }

    Popup {
        id: confirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Style.px(460)
        modal: true
        padding: Style.px(22)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }
        contentItem: ColumnLayout {
            spacing: Style.px(12)
            Txt { text: qsTr("Restore this version?"); font.pixelSize: Style.fontLead; font.weight: Font.DemiBold }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: tab.pending
                      ? qsTr("Mods, resource packs, shader packs and the game version go back to how they were: %1 (%2). Worlds and settings are not touched. The current state is remembered first, so you can come back to it.")
                            .arg(tab.whenText(tab.pending)).arg(tab.diffText(tab.pending))
                      : ""
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Style.px(8)
                QuietButton { text: qsTr("Cancel"); onClicked: confirm.close() }
                PrimaryButton {
                    implicitHeight: Style.px(40)
                    text: qsTr("Restore")
                    onClicked: {
                        confirm.close()
                        if (!Store.restore(tab.instanceId, tab.pending.file))
                            Router.toast(qsTr("Couldn't restore. Close the game and try again."), true)
                    }
                }
            }
        }
    }

    Popup {
        id: missingDialog
        property var names: []
        function show(list) { names = list; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Style.px(460)
        modal: true
        padding: Style.px(22)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }
        contentItem: ColumnLayout {
            spacing: Style.px(12)
            Txt { text: qsTr("Restored, except for some files"); font.pixelSize: Style.fontLead; font.weight: Font.DemiBold }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: qsTr("These files were no longer on this computer and couldn't be downloaded again. Add them yourself if you need them:")
            }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: missingDialog.names.join("\n")
            }
            QuietButton { Layout.alignment: Qt.AlignRight; text: qsTr("Close"); onClicked: missingDialog.close() }
        }
    }

    // Busy note while files are being brought back.
    Rectangle {
        visible: Store.restoring === tab.instanceId
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Style.px(12) }
        width: busy.implicitWidth + Style.px(28)
        height: busy.implicitHeight + Style.px(16)
        radius: height / 2
        color: Style.surfaceHi
        Txt { id: busy; anchors.centerIn: parent; text: qsTr("Restoring…") }
    }
}
