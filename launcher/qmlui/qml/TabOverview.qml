import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// Name, icon and everyday actions for one instance.
Item {
    id: tab
    property string instanceId
    property var instance

    component Action: QuietButton { Layout.fillWidth: true; implicitHeight: Style.px(44) }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: Math.min(parent.width, Style.px(720))
            spacing: Style.px(14)

            Txt { text: qsTr("Name"); font.weight: Font.DemiBold }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(10)
                Field {
                    id: nameField
                    Layout.fillWidth: true
                    text: tab.instance ? tab.instance.name : ""
                    onAccepted: Launcher.renameInstance(tab.instanceId, text)
                }
                QuietButton {
                    text: qsTr("Rename")
                    enabled: tab.instance && nameField.text.trim().length > 0 && nameField.text !== tab.instance.name
                    onClicked: Launcher.renameInstance(tab.instanceId, nameField.text)
                }
                QuietButton { iconName: "image"; text: qsTr("Change icon"); onClicked: Launcher.changeIcon(tab.instanceId) }
            }

            Txt { text: qsTr("Actions"); font.weight: Font.DemiBold; Layout.topMargin: Style.px(8) }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                uniformCellWidths: true
                columnSpacing: Style.px(10)
                rowSpacing: Style.px(10)
                Action { iconName: "folder"; text: qsTr("Open folder"); onClicked: Launcher.openInstanceFolder(tab.instanceId) }
                Action { iconName: "terminal"; text: qsTr("Game log"); onClicked: Launcher.showConsole(tab.instanceId) }
                Action { iconName: "play"; text: qsTr("Play offline"); onClicked: Launcher.launch(tab.instanceId, true) }
                Action { iconName: "copy"; text: qsTr("Copy instance"); onClicked: Launcher.copyInstance(tab.instanceId) }
                Action { iconName: "export"; text: qsTr("Export"); onClicked: Launcher.exportInstance(tab.instanceId) }
                Action { iconName: "trash"; text: qsTr("Delete"); danger: true; onClicked: Launcher.deleteInstance(tab.instanceId) }
            }

            Txt { text: qsTr("Version"); font.weight: Font.DemiBold; Layout.topMargin: Style.px(8) }
            RowLayout {
                spacing: Style.px(12)
                Txt { text: Fmt.versionLine(tab.instance) || qsTr("Unknown version"); font.pixelSize: Style.fontLead }
                QuietButton { text: qsTr("Change version or loader"); onClicked: Launcher.editInstance(tab.instanceId, "version") }
            }

            Txt {
                visible: !!(tab.instance && tab.instance.notes)
                text: qsTr("Notes"); font.weight: Font.DemiBold; Layout.topMargin: Style.px(8)
            }
            Txt {
                visible: !!(tab.instance && tab.instance.notes)
                text: tab.instance ? tab.instance.notes : ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                muted: true
            }
        }
    }
}
