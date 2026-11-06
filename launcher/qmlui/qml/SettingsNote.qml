import QtQuick
import QtQuick.Layouts

// An inline message: info, warning (accent) or danger.
Rectangle {
    id: note
    property string text
    property string kind: "info"     // info | warn | danger
    readonly property color tone: kind === "danger" ? Style.danger : kind === "warn" ? Style.accent : Style.textMuted
    visible: text.length > 0
    Layout.fillWidth: true
    implicitHeight: t.implicitHeight + Style.px(18)
    radius: Style.radius
    color: Style.alpha(tone, 0.12)
    border.width: 1
    border.color: Style.alpha(tone, 0.6)
    Txt {
        id: t
        anchors { fill: parent; margins: Style.px(9); leftMargin: Style.px(12); rightMargin: Style.px(12) }
        text: note.text
        wrapMode: Text.WordWrap
        font.pixelSize: Style.fontSmall
    }
}
