import QtQuick
import QtQuick.Controls

// On/off switch in the theme's colors.
Switch {
    id: sw
    padding: 0
    implicitWidth: indicator.implicitWidth
    implicitHeight: indicator.implicitHeight
    hoverEnabled: true
    indicator: Rectangle {
        implicitWidth: Style.px(42); implicitHeight: Style.px(24); radius: height / 2
        color: sw.checked ? Style.accent : Style.line
        border.width: sw.visualFocus ? 2 : 0
        border.color: Style.text
        Behavior on color { ColorAnimation { duration: Style.dur(120) } }
        Rectangle {
            width: Style.px(18); height: width; radius: width / 2
            y: (parent.height - height) / 2
            x: sw.checked ? parent.width - width - 3 : 3
            color: sw.checked ? Style.onAccent : Style.text
            Behavior on x { NumberAnimation { duration: Style.dur(120) } }
        }
    }
    contentItem: Item {}
}
