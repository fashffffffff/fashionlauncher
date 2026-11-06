import QtQuick

// A small drawing of the launcher in a theme's colors: sidebar, title, a panel, a button and a hotbar.
// colors = { bg, surface, text, accent }; scales with its size.
Rectangle {
    id: prev
    property var colors: ({})
    readonly property color cBg: colors.bg || Style.bg
    readonly property color cSurface: colors.surface || Style.surface
    readonly property color cText: colors.text || Style.text
    readonly property color cAccent: colors.accent || Style.accent
    readonly property real u: width / 100       // one unit = 1% of the width
    readonly property real r: Math.max(2, u * 1.4)

    color: cBg
    clip: true

    // sidebar
    Rectangle {
        x: 0; y: 0; width: prev.u * 14; height: prev.height
        color: Qt.rgba(prev.cSurface.r, prev.cSurface.g, prev.cSurface.b, 0.9)
        Column {
            x: prev.u * 4; y: prev.u * 5
            spacing: prev.u * 3.2
            Repeater {
                model: 4
                Rectangle {
                    width: prev.u * 6; height: width; radius: prev.r
                    color: index === 0 ? prev.cAccent : Qt.rgba(prev.cText.r, prev.cText.g, prev.cText.b, 0.25)
                }
            }
        }
    }
    // title and a line of text
    Rectangle { x: prev.u * 20; y: prev.u * 7; width: prev.u * 34; height: prev.u * 5; radius: prev.r; color: prev.cText }
    Rectangle {
        x: prev.u * 20; y: prev.u * 15; width: prev.u * 24; height: prev.u * 2.6; radius: prev.r
        color: Qt.rgba(prev.cText.r, prev.cText.g, prev.cText.b, 0.45)
    }
    // play button
    Rectangle { x: prev.u * 20; y: prev.u * 22; width: prev.u * 20; height: prev.u * 7; radius: prev.r; color: prev.cAccent }
    // a panel on the right
    Rectangle {
        x: prev.u * 62; y: prev.u * 7; width: prev.u * 32; height: prev.height - prev.u * 22; radius: prev.r * 1.5
        color: prev.cSurface
        border.width: 1; border.color: Qt.rgba(prev.cText.r, prev.cText.g, prev.cText.b, 0.12)
        Column {
            x: prev.u * 3; y: prev.u * 3
            spacing: prev.u * 2.4
            Repeater {
                model: 4
                Rectangle {
                    width: prev.u * (index % 2 ? 18 : 24); height: prev.u * 2.4; radius: prev.r
                    color: Qt.rgba(prev.cText.r, prev.cText.g, prev.cText.b, index === 0 ? 0.8 : 0.35)
                }
            }
        }
    }
    // hotbar
    Row {
        x: prev.u * 20; y: prev.height - prev.u * 12
        spacing: prev.u * 1.6
        Repeater {
            model: 5
            Rectangle {
                width: prev.u * 7; height: width; radius: prev.r
                color: prev.cSurface
                border.width: index === 0 ? Math.max(1, prev.u * 0.6) : 1
                border.color: index === 0 ? prev.cAccent : Qt.rgba(prev.cText.r, prev.cText.g, prev.cText.b, 0.15)
            }
        }
    }
}
