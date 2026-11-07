import QtQuick
import QtQuick.Controls
import QtQuick.Effects

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

    readonly property real depth: Math.round(Style.blockDepth * 0.8)
    // the selected block stands up; the rest sit lower, showing less of their side
    readonly property real lift: selected ? 0 : (hovered ? depth * 0.35 : depth * 0.6)
    width: Style.slotSize * Style.scale
    height: width + depth
    topPadding: lift
    bottomPadding: depth - lift
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: tip
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 300

    background: Item {
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: parent.height - slot.depth
            radius: Style.slotRadius
            color: slot.selected ? Style.edge(Style.accent) : Style.alpha(Style.edge(Style.surfaceHi), 0.85)
        }
        Rectangle {
            y: slot.lift
            width: parent.width
            height: parent.height - slot.depth
            radius: Style.slotRadius
            color: slot.selected ? Style.mix(Style.surfaceHi, Style.accent, 0.16)
                 : slot.hovered ? Style.alpha(Style.surfaceHi, 0.9) : Style.alpha(Style.surface, 0.82)
            border.width: slot.selected ? 2 : 1
            border.color: slot.selected ? Style.accent : Style.alpha(Style.text, 0.1)
            Behavior on y { NumberAnimation { duration: Style.dur(110); easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: Style.dur(120) } }
        }
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
            // The selected item glows along its own silhouette, like an enchanted item in the hotbar
            layer.enabled: slot.selected
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Qt.lighter(Style.accent, 1.25)
                shadowBlur: 1.0
                blurMax: 40
                shadowScale: 1.22
                shadowOpacity: Style.dark ? 1.0 : 0.75
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 0
                autoPaddingEnabled: true
            }
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
