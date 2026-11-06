import QtQuick

// Round initial with an online dot.
Rectangle {
    id: av
    property string name
    property bool online: false
    property bool showDot: true
    implicitWidth: Style.px(40); implicitHeight: implicitWidth
    radius: width / 2
    color: Style.alpha(Style.accent, 0.22)
    Text {
        anchors.centerIn: parent
        text: (av.name || "?").charAt(0).toUpperCase()
        color: Style.text
        font.family: Style.display
        font.pixelSize: Math.round(av.height * 0.42)
    }
    Rectangle {
        visible: av.showDot
        width: Math.round(av.width * 0.3); height: width; radius: width / 2
        anchors { right: parent.right; bottom: parent.bottom }
        color: av.online ? Style.success : Style.line
        border.width: 2
        border.color: Style.surface
    }
}
