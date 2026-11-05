import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    padding: Style.px(6)
    background: Rectangle {
        implicitWidth: Style.px(220)
        radius: Style.radius
        color: Style.surface
        border.width: 1
        border.color: Style.line
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.dur(110) } }
}
