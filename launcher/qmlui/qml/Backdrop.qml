import QtQuick
import QtQuick.Effects

// Full-bleed scene behind everything. What it shows comes from the theme's "background" section
// (mode: wallpaper | image | video | slideshow | screenshot | panorama | shader | pattern | solid), unless the selected
// instance has its own background (Backgrounds.overrides). Anything that can't be shown — an
// instance without screenshots, a missing file, a paused video — falls back to a wallpaper,
// never to an empty block.
Item {
    id: root
    property string screenshot
    property string seed: "world"

    readonly property var cfg: Style.bgCfg
    readonly property var instanceBg: Backgrounds.overrides[seed] || null
    readonly property string mode: instanceBg ? instanceBg.kind : (cfg.mode || "screenshot")

    readonly property string fallbackImage: Backgrounds.wallpaperUrl(cfg.wallpaper || "")
                                            || Backgrounds.wallpaperUrl(Backgrounds.defaultWallpaper)

    // Slideshow: wallpaper ids or the user's own image files, switched every `interval` seconds.
    readonly property var slides: {
        const raw = cfg.slideshow
        const list = raw && typeof raw === "object" ? Array.from(raw) : []
        const urls = []
        for (const s of list) {
            const u = Backgrounds.wallpaperUrl(s) || Backgrounds.userFileUrl(s)
            if (u) urls.push(u)
        }
        return urls.length > 0 ? urls : Backgrounds.wallpapers.map(w => w.url)
    }
    property int slide: 0
    Timer {
        interval: Math.max(5, Number(root.cfg.interval) || 60) * 1000
        repeat: true
        running: root.mode === "slideshow" && root.slides.length > 1 && root.windowShown && !root.gameRunning
        onTriggered: root.slide = (root.slide + 1) % root.slides.length
    }

    // Video
    readonly property string videoUrl: mode !== "video" ? ""
        : instanceBg ? Backgrounds.resolve("video", instanceBg.value) : Backgrounds.userFileUrl(cfg.video || "")
    readonly property bool windowShown: Window.window !== null && Window.window.visible
                                        && Window.window.visibility !== Window.Minimized
                                        && Window.window.visibility !== Window.Hidden
    readonly property bool gameRunning: {
        const l = Launcher.launches
        for (const id in l) if (l[id].phase === "running") return true
        return false
    }
    // Unloaded (decoder freed) while nobody can see it; paused on pages where it sits under a dark veil.
    readonly property bool videoLoaded: videoUrl !== "" && windowShown && !gameRunning
    readonly property bool videoPaused: Router.section !== "home" && Router.section !== "appearance"
    readonly property bool videoShowing: videoLoaded && video.item !== null && video.item.showing && !video.item.failed

    // Moving backgrounds (panorama, shader) only move where they can be seen clearly.
    readonly property bool animate: windowShown && !gameRunning && !videoPaused
    readonly property real speed: cfg.speed !== undefined ? Number(cfg.speed) : 1

    // Panorama: six cube faces found by Backgrounds for the selected instance (or the user's own).
    readonly property var panoramaFaces: mode === "panorama" && Backgrounds.panorama.faces ? Backgrounds.panorama.faces : []
    readonly property bool panoramaShowing: panorama.item !== null && panorama.item.ready
    function reloadPanorama() { if (mode === "panorama") Backgrounds.loadPanorama(seed) }
    onModeChanged: reloadPanorama()
    onSeedChanged: reloadPanorama()
    // A game that just closed may have captured a new panorama.
    onGameRunningChanged: if (!gameRunning) reloadPanorama()
    Connections {
        target: Backgrounds
        function onPanoramaSourceChanged() { root.reloadPanorama() }
        function onCustomPanoramasChanged() { root.reloadPanorama() }
    }

    readonly property string imageSource: {
        switch (mode) {
        case "wallpaper": return (instanceBg ? Backgrounds.wallpaperUrl(instanceBg.value) : Backgrounds.wallpaperUrl(cfg.wallpaper || "")) || fallbackImage
        case "image": return (instanceBg ? Backgrounds.userFileUrl(instanceBg.value) : Backgrounds.userFileUrl(cfg.image || "")) || fallbackImage
        case "slideshow": return slides.length > 0 ? slides[slide % slides.length] : fallbackImage
        case "screenshot": return screenshot || fallbackImage
        case "video": return videoShowing ? "" : fallbackImage
        // The front face stays underneath (hidden once the cube shows) so the accent can follow it.
        case "panorama": return panoramaFaces.length === 6 ? panoramaFaces[0] : (Backgrounds.panorama.busy ? "" : fallbackImage)
        default: return ""   // shader, pattern, solid
        }
    }
    readonly property bool showPattern: mode === "pattern"
        || (mode !== "solid" && mode !== "shader" && mode !== "panorama" && imageSource === "" && !videoShowing)

    Rectangle { anchors.fill: parent; color: root.mode === "solid" && root.cfg.color ? root.cfg.color : Style.bg }

    // Blurred backgrounds are rendered at a quarter of the size and scaled up: after a strong blur
    // the difference is invisible, and it needs 16x less memory than full-resolution blur buffers.
    readonly property bool blurOn: (cfg.blur || 0) > 0
    readonly property int down: blurOn ? 4 : 1

    // Two images cross-fade so switching instances never flashes; a video sits underneath them.
    Item {
        id: photo
        width: Math.ceil(root.width / root.down)
        height: Math.ceil(root.height / root.down)
        // Rendered into a layer of its own (quarter size when blurred), then scaled up with the effect.
        // A layer instead of a hidden MultiEffect source: hidden items don't receive new video frames.
        scale: root.down
        transformOrigin: Item.TopLeft
        visible: root.imageSource !== "" || root.videoShowing || imgA.source != "" || imgB.source != ""
                 || root.mode === "shader" || root.mode === "panorama"
        // Moving backgrounds without blur skip the window-sized effect layer: re-rendering it every frame
        // is the most expensive part of an animated background.
        layer.enabled: root.blurOn || (root.mode !== "shader" && root.mode !== "panorama")
        layer.smooth: true
        layer.effect: MultiEffect {
            blurEnabled: root.blurOn
            blurMax: 16
            blur: Math.min(1, (root.cfg.blur || 0) / 64)
            saturation: -0.1
            autoPaddingEnabled: false
        }

        Loader {
            id: video
            anchors.fill: parent
            active: root.videoLoaded
            sourceComponent: BgVideo {
                source: root.videoUrl
                paused: root.videoPaused
            }
        }

        Loader {
            id: panorama
            anchors.fill: parent
            active: root.mode === "panorama" && root.panoramaFaces.length === 6 && root.windowShown && !root.gameRunning
            sourceComponent: PanoramaView {
                faces: root.panoramaFaces
                running: root.animate
                speed: root.speed
            }
        }
        Loader {
            anchors.fill: parent
            active: root.mode === "shader" && root.windowShown && !root.gameRunning
            sourceComponent: ShaderBackground {
                preset: root.cfg.shader || "aurora"
                running: root.animate
                speed: root.speed
                seed: root.seed
            }
        }

        property bool flip: false
        Image {
            id: imgA
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            // Never decode more pixels than the screen shows.
            sourceSize.width: Math.min(1920, Math.max(320, photo.width))
            opacity: photo.flip ? 0 : 1
            visible: !root.panoramaShowing
            Behavior on opacity { NumberAnimation { duration: Style.dur(450); easing.type: Easing.OutCubic } }
        }
        Image {
            id: imgB
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            sourceSize.width: imgA.sourceSize.width
            opacity: photo.flip ? 1 : 0
            visible: !root.panoramaShowing
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
    Component.onCompleted: { imgA.source = imageSource; reloadPanorama() }

    Loader {
        anchors.fill: parent
        active: root.showPattern
        sourceComponent: BgBlocks { seed: root.seed }
    }

    // Legibility: darken toward the bottom-left where the title and controls sit.
    Rectangle {
        anchors.fill: parent
        visible: root.mode !== "solid"
        gradient: Gradient {
            GradientStop { position: 0.0; color: Style.alpha(Style.scrim, (root.cfg.dim || 0) * 0.35) }
            GradientStop { position: 0.55; color: Style.alpha(Style.scrim, (root.cfg.dim || 0) * 0.7) }
            GradientStop { position: 1.0; color: Style.alpha(Style.scrim, Math.min(1, (root.cfg.dim || 0) + 0.3)) }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: root.mode !== "solid"
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Style.alpha(Style.scrim, 0.55) }
            GradientStop { position: 0.6; color: Style.alpha(Style.scrim, 0.0) }
        }
    }
}
