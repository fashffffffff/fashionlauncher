import QtQuick
import QtQuick.Controls

// Secondary action: icon, optional label, translucent surface.
Button {
    id: control
    property string iconName
    property string tip
    property bool danger: false
    property bool plain: false
    /// UI sound on click ("" = none)
    property string sound: "click"
    onClicked: if (sound.length) Sounds.play(sound)
    readonly property color ink: danger ? Style.danger : Style.text

    implicitHeight: Style.px(40)
    implicitWidth: text.length ? contentItem.implicitWidth + Style.px(28) : implicitHeight
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    ToolTip.visible: (tip.length > 0 || (btnText.truncated && control.text.length > 0)) && hovered
    ToolTip.text: tip.length > 0 ? tip : control.text
    ToolTip.delay: 500
    Accessible.name: text.length ? text : tip

    background: Rectangle {
        radius: Style.radius
        color: control.down ? Style.alpha(Style.text, 0.16)
             : control.hovered ? Style.alpha(Style.text, 0.10)
             : control.plain ? "transparent" : Style.alpha(Style.surface, 0.72)
        border.width: control.visualFocus ? 2 : (control.plain ? 0 : 1)
        border.color: control.visualFocus ? Style.accent : Style.alpha(Style.text, 0.08)
        Behavior on color { ColorAnimation { duration: Style.dur(120) } }
    }
    contentItem: Item {
      // from the children's own sizes: the Row's implicitWidth depends on btnText.width, which depends on the Row
      implicitWidth: (btnIcon.visible ? btnIcon.width : 0) + (btnText.visible ? btnText.implicitWidth : 0)
                     + (btnIcon.visible && btnText.visible ? row.spacing : 0)
      implicitHeight: row.implicitHeight
      clip: true
      Row {
        id: row
        spacing: Style.px(8)
        anchors.centerIn: parent
        width: Math.min(parent.implicitWidth, Math.max(0, parent.width - Style.px(16)))
        Icon {
            id: btnIcon
            visible: control.iconName.length > 0
            name: control.iconName
            color: control.ink
            size: Style.px(18)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: btnText
            visible: control.text.length > 0
            text: control.text
            color: control.ink
            font.family: Style.ui
            font.pixelSize: Style.fontBody
            font.weight: Font.Medium
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, Math.max(0, control.availableWidth - Style.px(16) - (btnIcon.visible ? btnIcon.width + row.spacing : 0)))
            elide: Text.ElideRight
        }
      }
    }
}
