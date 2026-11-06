import QtQuick
import QtQuick.Controls

// One tile of the background gallery: a wallpaper, the user's picture or video.
AbstractButton {
    id: tile
    property string url              // image to preview ("" for videos)
    property string title
    property bool video: false
    property bool selected: false
    property bool removable: false
    signal removeRequested()

    implicitWidth: Style.px(180)
    implicitHeight: Math.round(width * 9 / 16)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: title
    Accessible.checkable: true
    Accessible.checked: selected

    background: Rectangle {
        radius: Style.radius
        color: Style.surfaceHi
        clip: true

        Image {
            anchors.fill: parent
            visible: !tile.video
            source: tile.video ? "" : tile.url
            // Thumbnails are decoded small: 12 wallpapers cost ~3 MB, not ~100.
            sourceSize.width: 384
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Style.dur(200) } }
        }
        Rectangle {
            anchors.fill: parent
            visible: tile.video
            gradient: Gradient {
                GradientStop { position: 0; color: Style.mix(Style.surfaceHi, Style.accent, 0.25) }
                GradientStop { position: 1; color: Style.surface }
            }
            Icon { anchors.centerIn: parent; name: "play"; size: Style.px(28); color: Style.text }
        }
        // Caption on a soft shade so it reads on any picture.
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: caption.implicitHeight + Style.px(14)
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: Style.alpha(Style.scrim, 0.75) }
            }
            visible: tile.title.length > 0
            Txt {
                id: caption
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Style.px(8) }
                text: tile.title
                color: "white"
                font.pixelSize: Style.fontSmall
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: Style.radius
            border.width: tile.selected ? Style.px(3) : tile.visualFocus ? 2 : 1
            border.color: tile.selected || tile.visualFocus ? Style.accent : Style.alpha(Style.text, tile.hovered ? 0.3 : 0.1)
        }
        Rectangle {
            visible: tile.selected
            anchors { top: parent.top; right: parent.right; margins: Style.px(8) }
            width: Style.px(24); height: width; radius: width / 2
            color: Style.accent
            Icon { anchors.centerIn: parent; name: "check"; size: Style.px(16); color: Style.onAccent; weight: 2.4 }
        }
    }

    QuietButton {
        visible: tile.removable && (tile.hovered || hovered)
        anchors { top: parent.top; left: parent.left; margins: Style.px(6) }
        implicitHeight: Style.px(30)
        iconName: "trash"
        danger: true
        tip: qsTr("Remove")
        onClicked: tile.removeRequested()
    }
}
