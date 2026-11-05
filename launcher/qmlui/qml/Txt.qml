import QtQuick

// Body text with theme defaults.
Text {
    property bool muted: false
    color: muted ? Style.textMuted : Style.text
    font.family: Style.ui
    font.pixelSize: Style.fontBody
    wrapMode: Text.NoWrap
    elide: Text.ElideRight
}
