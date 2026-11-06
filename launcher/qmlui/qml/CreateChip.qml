import QtQuick
import QtQuick.Controls

// On/off filter chip with a check mark, e.g. "Snapshots".
AbstractButton {
    id: chip
    checkable: true
    implicitHeight: Style.px(32)
    implicitWidth: row.implicitWidth + Style.px(24)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checked: checked

    background: Rectangle {
        radius: height / 2
        color: chip.checked ? Style.alpha(Style.accent, 0.22) : chip.hovered ? Style.alpha(Style.text, 0.08) : Style.panel(0)
        border.width: chip.visualFocus ? 2 : 1
        border.color: chip.visualFocus ? Style.accent : chip.checked ? Style.alpha(Style.accent, 0.6) : Style.panelBorder
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Style.px(6)
            Icon {
                visible: chip.checked
                name: "check"
                size: Style.px(14)
                color: Style.text
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: chip.text
                color: Style.text
                font.family: Style.ui
                font.pixelSize: Style.fontSmall + 1
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
