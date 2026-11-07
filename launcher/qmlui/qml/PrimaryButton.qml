import QtQuick
import QtQuick.Controls

// The one loud button on a screen. Built as a block: a face on top of its darker lower side, and it presses
// down by that depth when clicked (Style.blockDepth; 0 = flat).
Button {
    id: control
    property string iconName
    /// 0..1 shows a fill; negative means indeterminate; undefined hides it
    property var progress: undefined
    property color fill: Style.accent
    property color ink: Style.inkOn(fill, Style.onAccent)
    /// UI sound on click ("" = none)
    property string sound: "click"
    onClicked: if (sound.length) Sounds.play(sound)

    readonly property real depth: enabled ? Style.blockDepth : Style.blockDepth * 0.4
    readonly property bool sunk: control.down || control.checked
    readonly property real sink: sunk ? depth * 0.8 : (hovered ? -Style.px(1) : 0)

    implicitHeight: Style.px(48) + depth
    implicitWidth: Math.max(Style.px(160), row.implicitWidth + Style.px(40))
    padding: 0
    // the content rides on the face
    topPadding: Math.max(0, sink)
    bottomPadding: depth - sink
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text

    background: Item {
        // lower side of the block
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: parent.height - control.depth
            radius: Style.radius
            color: Style.edge(control.fill)
            visible: control.depth > 0
        }
        // face
        Rectangle {
            id: face
            x: 0
            y: control.sink
            width: parent.width
            height: parent.height - control.depth
            radius: Style.radius
            color: control.hovered && !control.sunk ? Qt.lighter(control.fill, 1.06) : control.fill
            border.width: control.visualFocus ? 2 : 0
            border.color: Style.text
            clip: true
            Behavior on y { NumberAnimation { duration: Style.dur(70) } }
            Behavior on color { ColorAnimation { duration: Style.dur(120) } }

            // progress fill
            Rectangle {
                visible: control.progress !== undefined
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: control.progress >= 0 ? parent.width * control.progress : parent.width * 0.3
                color: Style.alpha(control.ink, 0.16)
                radius: parent.radius
                Behavior on width { NumberAnimation { duration: Style.dur(200) } }
                // indeterminate sweep
                SequentialAnimation on x {
                    running: control.progress !== undefined && control.progress < 0 && Style.motion
                    loops: Animation.Infinite
                    NumberAnimation { from: -control.width * 0.3; to: control.width; duration: 1400; easing.type: Easing.InOutQuad }
                }
            }
        }
    }
    contentItem: Item {
        clip: true
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Style.px(10)
            Icon {
                id: icon
                visible: control.iconName.length > 0
                name: control.iconName
                color: control.ink
                size: Style.px(18)
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: control.text
                color: control.ink
                // Long translations shrink a little before eliding instead of touching the edges
                width: Math.min(implicitWidth, control.width - Style.px(28) - (icon.visible ? icon.width + row.spacing : 0))
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: Math.round(Style.fontLead * 0.8)
                font.family: Style.ui
                font.pixelSize: Style.fontLead
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
            }
        }
    }
}
