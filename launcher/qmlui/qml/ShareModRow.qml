import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One tickable mod in ShareModList.
AbstractButton {
    id: row
    property var entry: ({})
    property bool on: true
    property string sideLabel
    readonly property bool local: entry.platform === "local"
    implicitHeight: Style.px(local ? 54 : 44)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: entry.name || ""
    Accessible.role: Accessible.CheckBox
    Accessible.checked: on

    background: Rectangle {
        radius: Style.radius - 2
        color: row.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
        border.width: row.visualFocus ? 2 : 0
        border.color: Style.accent
    }
    contentItem: RowLayout {
        spacing: Style.px(12)
        Rectangle {
            Layout.leftMargin: Style.px(10)
            implicitWidth: Style.px(20); implicitHeight: Style.px(20)
            radius: Style.px(5)
            color: row.on ? Style.accent : "transparent"
            border.width: row.on ? 0 : 2
            border.color: Style.alpha(Style.text, 0.35)
            Icon { anchors.centerIn: parent; visible: row.on; name: "check"; size: Style.px(14); color: Style.onAccent }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Txt {
                Layout.fillWidth: true
                text: row.entry.name || row.entry.file || ""
                elide: Text.ElideRight
                opacity: row.on ? 1 : 0.55
            }
            Txt {
                Layout.fillWidth: true
                visible: row.local
                text: qsTr("Not on Modrinth or CurseForge — only the sender has this file")
                color: Style.danger
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
                opacity: row.on ? 1 : 0.55
            }
        }
        Rectangle {
            Layout.rightMargin: Style.px(10)
            visible: row.sideLabel.length > 0
            implicitHeight: Style.px(22)
            implicitWidth: sideText.implicitWidth + Style.px(16)
            radius: height / 2
            color: Style.alpha(row.entry.side === "client" ? Style.accent : Style.text, 0.10)
            Txt {
                id: sideText
                anchors.centerIn: parent
                text: row.sideLabel
                font.pixelSize: Style.fontSmall
                muted: row.entry.side !== "client"
            }
        }
    }
}
