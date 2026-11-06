import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Base of every dialog in the new UI: a centered card over a dimmed window.
// Put the body in `content` (a ColumnLayout); `title` and `subtitle` head it.
Popup {
    id: card
    property string title
    property string subtitle
    property bool dismissable: true
    default property alias content: body.data

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Style.px(48) : 520, Style.px(480))
    modal: true
    focus: true
    padding: Style.px(24)
    closePolicy: dismissable ? Popup.CloseOnEscape | Popup.CloseOnPressOutside : Popup.NoAutoClose
    background: Rectangle { radius: Style.radius + Style.px(4); color: Style.surface; border.width: 1; border.color: Style.line }
    Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.dur(140) }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Style.dur(180); easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.dur(110) } }

    contentItem: ColumnLayout {
        spacing: Style.px(14)
        Text {
            visible: card.title.length > 0
            Layout.fillWidth: true
            text: card.title
            color: Style.text
            font.family: Style.display
            font.pixelSize: Style.fontTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Txt {
            visible: card.subtitle.length > 0
            Layout.fillWidth: true
            text: card.subtitle
            muted: true
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(12)
        }
    }
}
