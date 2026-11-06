import QtQuick
import QtQuick.Controls

// Row of mutually exclusive choices: options = [{ value, label }]
Rectangle {
    id: seg
    property var options: []
    property string value
    property real itemHeight: Style.px(36)
    signal picked(string value)

    implicitWidth: row.implicitWidth + Style.px(8)
    implicitHeight: itemHeight + Style.px(8)
    radius: Style.radius
    color: Style.panel(0)
    border.width: 1
    border.color: Style.panelBorder

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Style.px(2)
        Repeater {
            model: seg.options
            delegate: AbstractButton {
                id: segBtn
                readonly property bool chosen: modelData.value === seg.value
                implicitHeight: seg.itemHeight
                implicitWidth: segText.implicitWidth + Style.px(28)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.label
                Accessible.role: Accessible.RadioButton
                Accessible.checked: chosen
                onClicked: seg.picked(modelData.value)
                background: Rectangle {
                    radius: Style.radius - 2
                    color: segBtn.chosen ? Style.accent : segBtn.hovered ? Style.alpha(Style.text, 0.08) : "transparent"
                    border.width: segBtn.visualFocus ? 2 : 0
                    border.color: Style.text
                    Behavior on color { ColorAnimation { duration: Style.dur(120) } }
                }
                contentItem: Text {
                    id: segText
                    text: modelData.label
                    color: segBtn.chosen ? Style.onAccent : Style.text
                    font.family: Style.ui
                    font.pixelSize: Style.fontBody
                    font.weight: segBtn.chosen ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
