pragma Singleton
import QtQuick

// Resolved design tokens. Everything visual reads from here, and everything here
// comes from the active theme JSON, so a theme file controls the whole look.
QtObject {
    readonly property var c: Theme.colors
    readonly property var sh: Theme.shape
    readonly property var ty: Theme.type
    readonly property var bgCfg: Theme.background
    readonly property var mo: Theme.motion

    readonly property color bg: c.bg
    readonly property color surface: c.surface
    readonly property color surfaceHi: c.surfaceHi
    readonly property color line: c.line
    readonly property color text: c.text
    readonly property color textMuted: c.textMuted
    readonly property color accent: c.accent
    readonly property color onAccent: c.onAccent
    readonly property color danger: c.danger
    readonly property color success: c.success
    readonly property color scrim: c.scrim

    readonly property real radius: sh.radius
    readonly property real slotRadius: sh.slotRadius
    readonly property real slotSize: sh.slotSize

    readonly property string display: ty.display
    readonly property string ui: ty.ui
    readonly property real scale: ty.scale > 0 ? ty.scale : 1

    readonly property bool motion: mo.enabled !== false
    readonly property real speed: mo.speed > 0 ? mo.speed : 1

    // Type scale (perfect fourth, 1.333), multiplied by the user's UI scale
    function px(v) { return Math.round(v * scale) }
    readonly property int fontSmall: px(12)
    readonly property int fontBody: px(14)
    readonly property int fontLead: px(17)
    readonly property int fontTitle: px(22)
    readonly property int fontHero: px(52)

    function dur(ms) { return motion ? ms / speed : 0 }
    function alpha(col, a) { const q = Qt.lighter(col, 1.0); return Qt.rgba(q.r, q.g, q.b, a) }
    function mix(a, b, t) {
        const x = Qt.lighter(a, 1.0), y = Qt.lighter(b, 1.0)
        return Qt.rgba(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, 1)
    }
}
