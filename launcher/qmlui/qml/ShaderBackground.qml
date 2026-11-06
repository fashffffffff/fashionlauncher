import QtQuick

// Live shader background. The shader runs on a small texture (half or a third of the window) that is
// scaled up: these patterns are soft, so it looks the same and costs 4–9x less GPU time. Frames come
// from a 30 fps timer, and stop entirely while `running` is false.
Item {
    id: root
    property string preset: "aurora"     // aurora | starfield | clouds | plasma | blocks
    property bool running: true
    property real speed: 1
    property string seed: "world"
    property real time: 40                // a pleasant moment for still previews
    // Theme colors: deep background, accent, second tone.
    property color colorA: Style.mix(Style.bg, Style.accent, 0.08)
    property color colorB: Style.accent
    property color colorC: Style.mix(Style.surfaceHi, Style.accent, 0.35)

    readonly property real down: preset === "starfield" || preset === "blocks" ? 2 : 3

    function hash(s) {
        let h = 2166136261
        for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) }
        return (h >>> 0) % 1000 / 100
    }

    Timer {
        interval: 33
        repeat: true
        running: root.running && root.visible && root.speed > 0
        onTriggered: root.time += interval / 1000 * root.speed
    }

    Item {
        id: canvas
        width: Math.max(1, Math.ceil(root.width / root.down))
        height: Math.max(1, Math.ceil(root.height / root.down))
        scale: root.down
        transformOrigin: Item.TopLeft
        layer.enabled: true
        layer.smooth: true

        ShaderEffect {
            anchors.fill: parent
            readonly property real time: root.time
            readonly property real aspect: root.width / Math.max(1, root.height)
            readonly property real seed: root.hash(root.seed)
            readonly property color colorA: root.colorA
            readonly property color colorB: root.colorB
            readonly property color colorC: root.colorC
            fragmentShader: "qrc:/qmlui/shaders/" + root.preset + ".frag.qsb"
        }
    }
}
