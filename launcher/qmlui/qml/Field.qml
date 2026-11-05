import QtQuick
import QtQuick.Controls

TextField {
    id: control
    property string iconName
    implicitHeight: Style.px(40)
    leftPadding: iconName.length ? Style.px(38) : Style.px(12)
    rightPadding: Style.px(12)
    color: Style.text
    placeholderTextColor: Style.textMuted
    selectionColor: Style.alpha(Style.accent, 0.4)
    selectedTextColor: Style.text
    font.family: Style.ui
    font.pixelSize: Style.fontBody
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: Style.radius
        color: Style.alpha(Style.surface, 0.85)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Style.accent : Style.line
        Icon {
            visible: control.iconName.length > 0
            name: control.iconName
            color: Style.textMuted
            size: Style.px(18)
            anchors { left: parent.left; leftMargin: Style.px(12); verticalCenter: parent.verticalCenter }
        }
    }
}
