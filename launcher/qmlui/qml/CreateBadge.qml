import QtQuick

// Small rounded label, e.g. "Recommended" or "Snapshot".
Rectangle {
    id: badge
    property string text
    property color tint: Style.accent
    implicitWidth: label.implicitWidth + Style.px(14)
    implicitHeight: label.implicitHeight + Style.px(4)
    radius: height / 2
    color: Style.alpha(tint, 0.18)
    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text
        color: Style.text
        font.family: Style.ui
        font.pixelSize: Style.fontSmall
    }
}
