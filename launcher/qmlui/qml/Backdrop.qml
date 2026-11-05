import QtQuick
import QtQuick.Effects

// Full-bleed scene behind the Play screen: the instance's newest screenshot,
// a custom image, a generated block horizon, or a flat color — chosen by the theme.
Item {
    id: root
    property string screenshot
    property string seed: "world"

    readonly property string mode: Style.bgCfg.mode || "screenshot"
    readonly property string customImage: Style.bgCfg.image
        ? "file://" + Theme.userThemesDir + "/" + Style.bgCfg.image : ""
    readonly property string imageSource: mode === "screenshot" ? screenshot
                                        : mode === "image" ? customImage : ""
    readonly property bool showPattern: mode === "pattern" || (imageSource === "" && mode !== "solid")

    Rectangle { anchors.fill: parent; color: Style.bg }

    // Blurred backgrounds are rendered at a quarter of the size and scaled up: after a strong blur
    // the difference is invisible, and it needs 16x less memory than full-resolution blur buffers.
    readonly property bool blurOn: (Style.bgCfg.blur || 0) > 0
    readonly property int down: blurOn ? 4 : 1

    // Two images cross-fade so switching instances never flashes.
    Item {
        id: photo
        width: Math.ceil(root.width / root.down)
        height: Math.ceil(root.height / root.down)
        visible: false
        property bool flip: false
        Image {
            id: imgA
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            sourceSize.width: root.blurOn ? 480 : 1920
            opacity: photo.flip ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Style.dur(450); easing.type: Easing.OutCubic } }
        }
        Image {
            id: imgB
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            sourceSize.width: root.blurOn ? 480 : 1920
            opacity: photo.flip ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Style.dur(450); easing.type: Easing.OutCubic } }
        }
    }
    onImageSourceChanged: {
        if (photo.flip) { imgA.source = imageSource } else { imgB.source = imageSource }
        photo.flip = !photo.flip
        releaseHidden.restart()
    }
    // Free the decoded image that has faded out.
    Timer {
        id: releaseHidden
        interval: Style.dur(450) + 100
        onTriggered: { if (photo.flip) imgA.source = ""; else imgB.source = "" }
    }
    Component.onCompleted: { imgA.source = imageSource }

    MultiEffect {
        width: photo.width
        height: photo.height
        scale: root.down
        transformOrigin: Item.TopLeft
        source: photo
        visible: root.imageSource !== ""
        blurEnabled: root.blurOn
        blurMax: 16
        blur: Math.min(1, (Style.bgCfg.blur || 0) / 64)
        saturation: -0.1
        autoPaddingEnabled: false
    }

    // Generated block horizon: a skyline of square blocks, seeded by the instance id.
    Loader {
        anchors.fill: parent
        active: root.showPattern
        sourceComponent: blocksComponent
    }
    Component {
      id: blocksComponent
      Canvas {
        id: blocks
        renderStrategy: Canvas.Cooperative
        property color topColor: Style.mix(Style.surfaceHi, Style.accent, 0.55)
        property color midColor: Style.surfaceHi
        property color deepColor: Style.surface
        onTopColorChanged: requestPaint()
        onDeepColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections { target: root; function onSeedChanged() { blocks.requestPaint() } }

        function hash(s) {
            let h = 2166136261
            for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) }
            return h >>> 0
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const size = Math.max(18, Math.round(width / 56))
            const gap = Math.max(1, Math.round(size / 14))
            const cols = Math.ceil(width / size) + 1
            let rnd = hash(root.seed)
            const next = () => { rnd = (Math.imul(rnd, 1664525) + 1013904223) >>> 0; return rnd / 4294967296 }
            const baseRows = Math.max(3, Math.round(height * 0.2 / size))
            let h = baseRows
            for (let x = 0; x < cols; x++) {
                // random walk with plateaus, like terrain
                const r = next()
                if (r < 0.18) h = Math.max(2, h - 1)
                else if (r > 0.82) h = Math.min(Math.round(baseRows * 1.8), h + 1)
                for (let y = 0; y < h; y++) {
                    const depth = h - 1 - y
                    const px = x * size, py = height - (y + 1) * size
                    if (depth === 0) {
                        ctx.fillStyle = topColor
                        ctx.globalAlpha = 0.55 + next() * 0.2
                    } else {
                        ctx.fillStyle = depth < 3 ? midColor : deepColor
                        // fade into the ground the deeper it goes
                        ctx.globalAlpha = Math.max(0.12, 0.5 - depth * 0.07) * (0.8 + next() * 0.4)
                    }
                    ctx.fillRect(px, py, size - gap, size - gap)
                }
            }
        }
    }

    }

    // Legibility: darken toward the bottom-left where the title and controls sit.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Style.alpha(Style.scrim, (Style.bgCfg.dim || 0) * 0.35) }
            GradientStop { position: 0.55; color: Style.alpha(Style.scrim, (Style.bgCfg.dim || 0) * 0.7) }
            GradientStop { position: 1.0; color: Style.alpha(Style.scrim, Math.min(1, (Style.bgCfg.dim || 0) + 0.3)) }
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Style.alpha(Style.scrim, 0.55) }
            GradientStop { position: 0.6; color: Style.alpha(Style.scrim, 0.0) }
        }
    }
}
