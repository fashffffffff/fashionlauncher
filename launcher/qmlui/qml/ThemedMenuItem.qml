import QtQuick
import QtQuick.Controls

MenuItem {
    id: item
    property string iconName
    property bool danger: false
    implicitHeight: Style.px(36)
    leftPadding: Style.px(10)
    contentItem: Row {
        spacing: Style.px(10)
        Icon {
            name: item.iconName
            size: Style.px(16)
            color: item.danger ? Style.danger : Style.textMuted
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: item.text
            color: item.danger ? Style.danger : Style.text
            font.family: Style.ui
            font.pixelSize: Style.fontBody
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    background: Rectangle {
        radius: Style.radius - 4
        color: item.highlighted ? Style.alpha(Style.text, 0.08) : "transparent"
    }
}
