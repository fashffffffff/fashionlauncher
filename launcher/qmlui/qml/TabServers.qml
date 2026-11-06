import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FFPrism.Servers

// The instance's multiplayer server list (the same one the game shows): check status, join, add, edit, reorder.
Item {
    id: tab
    property string instanceId
    property var instance

    ServersModel {
        id: servers
        instanceId: tab.instanceId
        onFailed: (reason) => Router.toast(reason, true)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(14)
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Txt {
                Layout.fillWidth: true
                muted: true
                text: servers.pinging ? qsTr("Checking servers…") : qsTr("%n server(s)", "", servers.count)
            }
            QuietButton { iconName: "refresh"; text: qsTr("Check all"); enabled: !servers.pinging && servers.count > 0; onClicked: servers.pingAll() }
            PrimaryButton { iconName: "plus"; text: qsTr("Add server"); implicitHeight: Style.px(40); onClicked: editor.openFor(-1, "", "") }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Style.px(8)
            model: servers
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            move: Transition { NumberAnimation { properties: "y"; duration: Style.dur(160); easing.type: Easing.OutCubic } }
            displaced: Transition { NumberAnimation { properties: "y"; duration: Style.dur(160); easing.type: Easing.OutCubic } }

            delegate: Rectangle {
                id: row
                required property int index
                required property string name
                required property string address
                required property string icon
                required property string status
                required property int players
                required property int maxPlayers
                required property int latency
                required property string motd
                required property string version
                required property string error
                width: list.width - Style.px(8)
                height: Style.px(84)
                radius: Style.radius
                color: Style.panel(1)
                border.width: 1
                border.color: Style.panelBorder

                RowLayout {
                    anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                    spacing: Style.px(14)
                    ServerFavicon { source: row.icon; Layout.preferredWidth: Style.px(56); Layout.preferredHeight: Style.px(56) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(3)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(10)
                            Txt { text: row.name; font.weight: Font.Medium; font.pixelSize: Style.fontLead; Layout.maximumWidth: row.width * 0.4 }
                            Txt { text: row.address; muted: true; font.pixelSize: Style.fontSmall; Layout.fillWidth: true }
                        }
                        Txt {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: row.status === "online" ? row.motd : row.status === "offline" ? row.error : ""
                            textFormat: row.status === "online" ? Text.StyledText : Text.PlainText
                            muted: row.status !== "online"
                            maximumLineCount: 1
                        }
                        ServerStatus {
                            Layout.fillWidth: true
                            status: row.status; players: row.players; maxPlayers: row.maxPlayers; latency: row.latency
                            extra: row.version
                        }
                    }
                    PrimaryButton {
                        implicitHeight: Style.px(40)
                        implicitWidth: Style.px(120)
                        iconName: "play"
                        text: qsTr("Join")
                        onClicked: servers.join(row.index)
                    }
                    QuietButton {
                        iconName: "more"
                        tip: qsTr("More actions")
                        onClicked: serverMenu.popup()
                        ThemedMenu {
                            id: serverMenu
                            ThemedMenuItem { text: qsTr("Edit"); iconName: "pencil"; onTriggered: editor.openFor(row.index, row.name, row.address) }
                            ThemedMenuItem { text: qsTr("Check again"); iconName: "refresh"; onTriggered: servers.ping(row.index) }
                            ThemedMenuItem { text: qsTr("Copy address"); iconName: "copy"; onTriggered: { clip.text = row.address; clip.selectAll(); clip.copy(); Router.toast(qsTr("Address copied")) } }
                            ThemedMenuItem { text: qsTr("Move up"); iconName: "arrow-left"; enabled: row.index > 0; onTriggered: servers.move(row.index, row.index - 1) }
                            ThemedMenuItem { text: qsTr("Move down"); iconName: "chevron-right"; enabled: row.index < servers.count - 1; onTriggered: servers.move(row.index, row.index + 1) }
                            ThemedMenuItem { text: qsTr("Remove"); iconName: "trash"; danger: true; onTriggered: confirm.ask(row.index, row.name) }
                        }
                    }
                }
            }

            Txt {
                anchors.centerIn: parent
                visible: servers.count === 0
                width: parent.width * 0.8
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("No servers yet. Add one with its address — it also shows up in the game's Multiplayer list.")
                muted: true
            }
        }
    }

    // Hidden helper for copying to the clipboard
    TextEdit { id: clip; visible: false }

    // Add or edit a server
    Popup {
        id: editor
        property int row: -1
        function openFor(r, n, a) {
            row = r; nameField.text = n; addressField.text = a; tried = false; open(); (r < 0 ? addressField : nameField).forceActiveFocus()
        }
        property bool tried: false
        readonly property string problem: servers.addressProblem(addressField.text)
        function submit() {
            tried = true
            if (problem.length) return
            const ok = row < 0 ? servers.add(nameField.text, addressField.text) : servers.edit(row, nameField.text, addressField.text)
            if (ok) close()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Style.px(440)
        modal: true
        padding: Style.px(22)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }
        contentItem: ColumnLayout {
            spacing: Style.px(10)
            Txt { text: editor.row < 0 ? qsTr("Add a server") : qsTr("Edit server"); font.pixelSize: Style.fontLead; font.weight: Font.DemiBold }
            Txt { text: qsTr("Address"); muted: true; font.pixelSize: Style.fontSmall; Layout.topMargin: Style.px(4) }
            Field {
                id: addressField
                Layout.fillWidth: true
                placeholderText: qsTr("play.example.com")
                onAccepted: editor.submit()
            }
            Txt {
                visible: editor.tried && editor.problem.length > 0
                text: editor.problem
                color: Style.danger
                font.pixelSize: Style.fontSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Txt { text: qsTr("Name (optional)"); muted: true; font.pixelSize: Style.fontSmall; Layout.topMargin: Style.px(4) }
            Field {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Minecraft Server")
                onAccepted: editor.submit()
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Style.px(8)
                spacing: Style.px(8)
                QuietButton { text: qsTr("Cancel"); onClicked: editor.close() }
                PrimaryButton { text: editor.row < 0 ? qsTr("Add server") : qsTr("Save"); implicitHeight: Style.px(40); onClicked: editor.submit() }
            }
        }
    }

    // Removing can't be undone, so ask first.
    Popup {
        id: confirm
        property int row: -1
        property string name
        function ask(r, n) { row = r; name = n; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Style.px(420)
        modal: true
        padding: Style.px(22)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }
        contentItem: ColumnLayout {
            spacing: Style.px(14)
            Txt { text: qsTr("Remove “%1”?").arg(confirm.name); font.pixelSize: Style.fontLead; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Txt { text: qsTr("It disappears from this instance's server list, in the launcher and in the game."); muted: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Style.px(8)
                QuietButton { text: qsTr("Cancel"); onClicked: confirm.close() }
                PrimaryButton { text: qsTr("Remove"); fill: Style.danger; ink: "#FFFFFF"; implicitHeight: Style.px(40); onClicked: { servers.remove(confirm.row); confirm.close() } }
            }
        }
    }
}
