import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// Every instance, searchable. Picking one sends you back to Play with it selected.
Item {
    id: page
    property int selectedIndex: -1
    signal pick(int index)
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }

    ColumnLayout {
        anchors { fill: parent; leftMargin: Style.px(56); rightMargin: Style.px(56); topMargin: Style.px(24) }
        spacing: Style.px(20)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            Text {
                text: qsTr("Library")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(34)
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
                onClicked: Launcher.newInstance()
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Instances
            readonly property int columns: Math.max(1, Math.floor(width / Style.px(240)))
            cellWidth: width / columns
            cellHeight: Style.px(108)
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
                    onClicked: page.pick(index)
                    onDoubleClicked: Launcher.launch(model.instanceId)
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: tileMenu.popup() }

                    background: Rectangle {
                        radius: Style.radius
                        color: tile.hovered ? Style.alpha(Style.surfaceHi, 0.95) : Style.alpha(Style.surface, 0.9)
                        border.width: index === page.selectedIndex ? 2 : 1
                        border.color: index === page.selectedIndex ? Style.accent : Style.alpha(Style.text, 0.07)
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
                            Txt {
                                text: model.name
                                Layout.fillWidth: true
                                font.pixelSize: Style.fontLead
                                font.weight: Font.Medium
                            }
                            Txt { text: Fmt.versionLine(model); muted: true; Layout.fillWidth: true }
                        }
                        Rectangle {
                            visible: model.running || Launcher.launches[model.instanceId] !== undefined
                            width: Style.px(8); height: width; radius: width / 2
                            color: Style.success
                        }
                    }
                    ThemedMenu {
                        id: tileMenu
                        ThemedMenuItem { text: qsTr("Play"); iconName: "play"; onTriggered: Launcher.launch(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Edit instance"); iconName: "sliders"; onTriggered: Launcher.editInstance(model.instanceId, "") }
                        ThemedMenuItem { text: qsTr("Open folder"); iconName: "folder"; onTriggered: Launcher.openInstanceFolder(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Copy instance"); iconName: "copy"; onTriggered: Launcher.copyInstance(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Delete"); iconName: "trash"; danger: true; onTriggered: Launcher.deleteInstance(model.instanceId) }
                    }
                }
            }

            Txt {
                anchors.centerIn: parent
                visible: grid.count === 0 && search.text.length > 0
                text: qsTr("No instances match “%1”").arg(search.text)
                muted: true
                font.pixelSize: Style.fontLead
            }
        }
    }
}
