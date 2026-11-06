import QtQuick
import QtQuick.Controls

// A row of mutually exclusive choices. options: [{ value, label }]
Rectangle {
    id: seg
    property var options: []
    property var value
    signal picked(var value)
    implicitWidth: row.implicitWidth + Style.px(6)
    implicitHeight: Style.px(40)
    radius: Style.radius
    color: Style.alpha(Style.surface, 0.85)
    border.width: 1
    border.color: Style.line

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 0
        Repeater {
            model: seg.options
            delegate: AbstractButton {
                id: b
                readonly property bool on: modelData.value === seg.value
                implicitHeight: Style.px(34)
                implicitWidth: t.implicitWidth + Style.px(26)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.label
                onClicked: seg.picked(modelData.value)
                background: Rectangle {
                    radius: Style.radius - 2
                    color: b.on ? Style.accent : b.hovered ? Style.alpha(Style.text, 0.08) : "transparent"
                    border.width: b.visualFocus ? 2 : 0
                    border.color: Style.text
                }
                contentItem: Text {
                    id: t
                    text: modelData.label
                    color: b.on ? Style.onAccent : Style.text
                    font.family: Style.ui
                    font.pixelSize: Style.fontBody
                    font.weight: b.on ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
