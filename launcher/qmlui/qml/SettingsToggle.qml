import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// On/off switch with an optional text label to its left. Emits toggled(on) on user action only.
RowLayout {
    id: tg
    property string label
    property bool checked: false
    signal toggled(bool on)
    spacing: Style.px(10)

    Txt {
        visible: tg.label.length > 0
        text: tg.label
        MouseArea { anchors.fill: parent; enabled: tg.enabled; onClicked: { sw.toggle(); sw.toggled() } }
    }
    Switch {
        id: sw
        checked: tg.checked
        padding: 0
        onToggled: { Sounds.play("toggle"); tg.toggled(checked) }
        Accessible.name: tg.label
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
        // keep the switch showing the real value even after the user flips it
        Connections { target: tg; function onCheckedChanged() { sw.checked = tg.checked } }
    }
}
