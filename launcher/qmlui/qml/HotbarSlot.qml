import QtQuick
import QtQuick.Controls

// One hotbar slot. Mirrors the in-game hotbar: a square cell, the selected one framed.
AbstractButton {
    id: slot
    property string iconSource
    property string glyph          // icon name for non-instance slots
    property int number: 0         // 1..9 key hint, 0 = none
    property bool selected: false
    property bool running: false
    property string tip
    onClicked: Sounds.play("click")

    width: Style.slotSize * Style.scale
    height: width
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: tip
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 300

    background: Rectangle {
        radius: Style.slotRadius
        color: slot.selected ? Style.alpha(Style.surfaceHi, 0.92)
             : slot.hovered ? Style.alpha(Style.surfaceHi, 0.75) : Style.alpha(Style.surface, 0.7)
        border.width: slot.selected ? 3 : 1
        border.color: slot.selected ? Style.text : Style.alpha(Style.text, 0.1)
        Behavior on color { ColorAnimation { duration: Style.dur(120) } }
    }
    contentItem: Item {
        Image {
            visible: slot.iconSource.length > 0
            anchors.centerIn: parent
            width: parent.width * 0.58
            height: width
            source: slot.iconSource
            sourceSize: Qt.size(width * 2, height * 2)
            smooth: false   // keep pixel-art icons crisp
            scale: slot.selected ? 1.08 : 1
            Behavior on scale { NumberAnimation { duration: Style.dur(140); easing.type: Easing.OutBack } }
        }
        Icon {
            visible: slot.glyph.length > 0
            anchors.centerIn: parent
            name: slot.glyph
            size: Style.px(22)
            color: slot.hovered ? Style.text : Style.textMuted
        }
        Text {
            visible: slot.number > 0
            text: slot.number
            anchors { left: parent.left; top: parent.top; margins: Style.px(6) }
            color: slot.selected ? Style.text : Style.textMuted
            font.family: Style.display
            font.pixelSize: Style.px(10)
        }
        Rectangle {
            visible: slot.running
            width: Style.px(8); height: width; radius: width / 2
            color: Style.success
            anchors { right: parent.right; top: parent.top; margins: Style.px(7) }
        }
    }
}
