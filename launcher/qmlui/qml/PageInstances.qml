import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// All instances. Click a card to open it; the play button on the card launches right away.
Item {
    id: page
    property var params: ({})
    Component.onDestruction: Instances.filter = ""

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(20)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            Text {
                text: qsTr("Instances")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(32)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Field {
                id: search
                iconName: "search"
                placeholderText: qsTr("Search instances")
                Layout.preferredWidth: Style.px(280)
                onTextChanged: Instances.filter = text
                Keys.onEscapePressed: text = ""
            }
            PrimaryButton {
                text: qsTr("Create instance")
                iconName: "plus"
                implicitHeight: Style.px(40)
                onClicked: Router.open("create")
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Instances
            readonly property int columns: Math.max(1, Math.floor(width / Style.px(280)))
            cellWidth: width / columns
            cellHeight: Style.px(112)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight
                AbstractButton {
                    id: tile
                    anchors { fill: parent; rightMargin: Style.px(12); bottomMargin: Style.px(12) }
                    hoverEnabled: true
                    Accessible.name: model.name
                    onClicked: Router.openInstance(model.instanceId)
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: tileMenu.popup() }

                    background: Rectangle {
                        radius: Style.radius
                        color: tile.hovered ? Style.panel(1) : Style.panel(0)
                        border.width: tile.visualFocus ? 2 : 1
                        border.color: tile.visualFocus ? Style.accent : Style.panelBorder
                    }
                    contentItem: RowLayout {
                        spacing: Style.px(14)
                        anchors { fill: parent; leftMargin: Style.px(14); rightMargin: Style.px(14) }
                        Image {
                            source: model.icon
                            Layout.preferredWidth: Style.px(48)
                            Layout.preferredHeight: Style.px(48)
                            sourceSize: Qt.size(96, 96)
                            smooth: false
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(4)
                            Txt { text: model.name; Layout.fillWidth: true; font.pixelSize: Style.fontLead; font.weight: Font.Medium }
                            Txt { text: Fmt.versionLine(model) || Fmt.playTime(model.playTime); muted: true; Layout.fillWidth: true }
                        }
                        // Launch without opening the instance
                        AbstractButton {
                            id: quickPlay
                            readonly property var st: Launcher.launches[model.instanceId]
                            Layout.preferredWidth: Style.px(40)
                            Layout.preferredHeight: Style.px(40)
                            hoverEnabled: true
                            Accessible.name: st ? qsTr("Stop") : qsTr("Play")
                            ToolTip.visible: hovered
                            ToolTip.text: st ? (st.phase === "running" ? qsTr("Stop") : st.status) : qsTr("Play")
                            onClicked: st ? Launcher.kill(model.instanceId) : Launcher.launch(model.instanceId)
                            background: Rectangle {
                                radius: width / 2
                                color: quickPlay.st ? Style.success : quickPlay.hovered ? Style.accent : Style.alpha(Style.accent, 0.85)
                            }
                            contentItem: Item {
                                Icon { anchors.centerIn: parent; name: quickPlay.st ? "stop" : "play"; size: Style.px(16); color: Style.onAccent }
                            }
                        }
                    }
                    ThemedMenu {
                        id: tileMenu
                        ThemedMenuItem { text: qsTr("Play"); iconName: "play"; onTriggered: Launcher.launch(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Open"); iconName: "sliders"; onTriggered: Router.openInstance(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Open folder"); iconName: "folder"; onTriggered: Launcher.openInstanceFolder(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Copy instance"); iconName: "copy"; onTriggered: Launcher.copyInstance(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Delete"); iconName: "trash"; danger: true; onTriggered: Launcher.deleteInstance(model.instanceId) }
                    }
                }
            }

            Txt {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: search.text.length > 0 ? qsTr("No instances match “%1”").arg(search.text) : qsTr("No instances yet. Create one to start playing.")
                muted: true
                font.pixelSize: Style.fontLead
            }
        }
    }
}
