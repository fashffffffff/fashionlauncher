import QtQuick
import QtQuick.Layouts
import FFPrism.Servers

// Servers of the selected instance on Home: up to three, with live status and a one-click Join.
// Hidden when the instance has no servers, so Home stays clean.
Item {
    id: root
    property string instanceId
    readonly property int shown: Math.min(3, servers.count)
    visible: instanceId.length > 0 && shown > 0
    implicitHeight: visible ? box.implicitHeight : 0

    ServersModel {
        id: servers
        instanceId: root.instanceId
        onFailed: (reason) => Router.toast(reason, true)
    }

    Rectangle {
        id: box
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        implicitHeight: col.implicitHeight + Style.px(20)
        height: implicitHeight
        radius: Style.radius + Style.px(4)
        color: Style.panel(0)
        border.width: 1
        border.color: Style.panelBorder

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(10) }
            spacing: Style.px(2)

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Style.px(6)
                Txt { text: qsTr("Servers"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                QuietButton {
                    plain: true
                    implicitHeight: Style.px(28)
                    iconName: "refresh"
                    tip: qsTr("Check again")
                    enabled: !servers.pinging
                    onClicked: servers.pingAll()
                }
                QuietButton {
                    plain: true
                    implicitHeight: Style.px(28)
                    text: servers.count > root.shown ? qsTr("All %1").arg(servers.count) : qsTr("Manage")
                    onClicked: Router.openInstance(root.instanceId, "servers")
                }
            }

            Repeater {
                model: servers
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
                    required property string motdPlain
                    visible: index < root.shown
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? Style.px(52) : 0
                    radius: Style.radius
                    color: rowHover.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors { fill: parent; leftMargin: Style.px(6); rightMargin: Style.px(4) }
                        spacing: Style.px(10)
                        ServerFavicon { source: row.icon; Layout.preferredWidth: Style.px(36); Layout.preferredHeight: Style.px(36) }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(2)
                            Txt { text: row.name; font.weight: Font.Medium; Layout.fillWidth: true }
                            ServerStatus {
                                Layout.fillWidth: true
                                status: row.status; players: row.players; maxPlayers: row.maxPlayers; latency: row.latency
                            }
                        }
                        QuietButton {
                            iconName: "play"
                            text: qsTr("Join")
                            implicitHeight: Style.px(34)
                            onClicked: servers.join(row.index)
                        }
                    }
                }
            }
        }
    }
}
