import QtQuick
import QtQuick.Layouts

// Numbered step heading: "1  Minecraft version" with an optional hint underneath.
RowLayout {
    id: step
    property int number: 1
    property string title
    property string hint
    spacing: Style.px(10)

    Rectangle {
        Layout.preferredWidth: Style.px(24)
        Layout.preferredHeight: Style.px(24)
        Layout.alignment: Qt.AlignTop
        radius: width / 2
        color: Style.alpha(Style.accent, 0.2)
        Text {
            anchors.centerIn: parent
            text: step.number
            color: Style.text
            font.family: Style.ui
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.px(2)
        Text {
            text: step.title
            color: Style.text
            font.family: Style.ui
            font.pixelSize: Style.fontLead
            font.weight: Font.DemiBold
        }
        Txt {
            visible: step.hint.length > 0
            text: step.hint
            muted: true
            font.pixelSize: Style.fontSmall
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }
}
