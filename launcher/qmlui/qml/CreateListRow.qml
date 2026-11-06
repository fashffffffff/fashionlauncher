import QtQuick
import QtQuick.Controls

// Selectable row used by the version lists.
ItemDelegate {
    id: rowItem
    property bool selected: false
    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: Style.px(40)
    hoverEnabled: true
    padding: 0
    background: Rectangle {
        radius: Style.radius - 4
        color: rowItem.selected ? Style.alpha(Style.accent, 0.24)
             : rowItem.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
        border.width: rowItem.visualFocus ? 2 : 0
        border.color: Style.accent
    }
}
