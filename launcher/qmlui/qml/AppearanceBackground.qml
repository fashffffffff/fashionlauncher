import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects

// Appearance → Background: where the picture comes from, the gallery, blur/darken, and the
// selected instance's own background. Everything is stored in the theme's "background" section:
//   mode       wallpaper | slideshow | screenshot | image | video | panorama | shader | pattern | solid
//   wallpaper  built-in wallpaper id (also the fallback for screenshot mode and missing files)
//   image      / video   file name inside the user themes folder
//   slideshow  [wallpaper ids or image file names], interval  seconds between slides
//   shader     aurora | starfield | clouds | plasma | blocks, speed  0..3 for panorama and shader
//   color      solid color, blur 0..64, dim 0..0.9
// The panorama's source (automatic or one of the user's own) is a launcher setting, see Backgrounds.panoramaSource.
ColumnLayout {
    id: bg
    spacing: Style.px(14)

    readonly property var cfg: Style.bgCfg
    readonly property string mode: cfg.mode || "screenshot"
    readonly property string wallpaper: cfg.wallpaper || Backgrounds.defaultWallpaper
    readonly property var slideIds: cfg.slideshow && typeof cfg.slideshow === "object" ? Array.from(cfg.slideshow) : []
    property string filter: "all"

    function set(key, value) { Theme.setToken("background", key, value) }

    component Heading: Text {
        color: Style.text
        font.family: Style.ui
        font.pixelSize: Style.fontLead
        font.weight: Font.DemiBold
        Layout.topMargin: Style.px(8)
    }

    // Wrapping row of choices, so seven modes still fit a narrow window.
    component Choices: Flow {
        id: ch
        property var options: []      // [{ value, label }]
        property string value
        signal picked(string value)
        spacing: Style.px(6)
        Repeater {
            model: ch.options
            delegate: AbstractButton {
                id: chBtn
                readonly property bool on: modelData.value === ch.value
                implicitHeight: Style.px(34)
                implicitWidth: chText.implicitWidth + Style.px(26)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.label
                Accessible.checkable: true
                Accessible.checked: on
                onClicked: ch.picked(modelData.value)
                background: Rectangle {
                    radius: Style.radius - 2
                    color: chBtn.on ? Style.accent : chBtn.hovered ? Style.alpha(Style.text, 0.10) : Style.alpha(Style.text, 0.04)
                    border.width: chBtn.visualFocus ? 2 : 1
                    border.color: chBtn.visualFocus ? Style.text : chBtn.on ? Style.accent : Style.alpha(Style.text, 0.08)
                }
                contentItem: Text {
                    id: chText
                    text: modelData.label
                    color: chBtn.on ? Style.onAccent : Style.text
                    font.family: Style.ui
                    font.pixelSize: Style.fontBody
                    font.weight: chBtn.on ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    component TokenSlider: RowLayout {
        id: ts
        property string label
        property real from: 0
        property real to: 1
        property real stepSize: 0.01
        property real value
        property string valueText: value.toFixed(2)
        signal moved(real value)
        spacing: Style.px(14)
        Layout.fillWidth: true
        Txt { text: ts.label; Layout.preferredWidth: Style.px(150) }
        Slider {
            id: sl
            Layout.fillWidth: true
            from: ts.from; to: ts.to; stepSize: ts.stepSize
            value: ts.value
            onMoved: ts.moved(value)
            Accessible.name: ts.label
            background: Rectangle {
                x: sl.leftPadding; y: sl.topPadding + sl.availableHeight / 2 - height / 2
                width: sl.availableWidth; height: Style.px(4); radius: 2
                color: Style.line
                Rectangle { width: sl.visualPosition * parent.width; height: parent.height; radius: 2; color: Style.accent }
            }
            handle: Rectangle {
                x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
                y: sl.topPadding + sl.availableHeight / 2 - height / 2
                width: Style.px(18); height: width; radius: width / 2
                color: Style.text
                border.width: sl.visualFocus ? 3 : 0
                border.color: Style.accent
            }
        }
        Txt {
            text: ts.valueText
            muted: true
            Layout.preferredWidth: Style.px(56)
            horizontalAlignment: Text.AlignRight
        }
    }

    Heading { text: qsTr("Background") }
    Choices {
        Layout.fillWidth: true
        value: bg.mode
        options: [{ value: "wallpaper", label: qsTr("Wallpaper") }, { value: "slideshow", label: qsTr("Slideshow") },
                  { value: "screenshot", label: qsTr("Screenshot") }, { value: "image", label: qsTr("My image") },
                  { value: "video", label: qsTr("Video") }, { value: "panorama", label: qsTr("Panorama") },
                  { value: "shader", label: qsTr("Animated") }, { value: "pattern", label: qsTr("Blocks") },
                  { value: "solid", label: qsTr("Solid") }]
        onPicked: (v) => {
            if (v === "image" && !Backgrounds.userFileUrl(bg.cfg.image || "")) imageDialog.open()
            else if (v === "video" && !Backgrounds.userFileUrl(bg.cfg.video || "")) videoDialog.open()
            else bg.set("mode", v)
        }
    }
    Txt {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        muted: true
        text: ({
            wallpaper: qsTr("One picture behind everything. Pick it below."),
            slideshow: qsTr("Wallpapers take turns. Tick the ones you want below; none ticked means all of them."),
            screenshot: qsTr("Shows the newest screenshot of the selected instance. Instances without screenshots show the wallpaper selected below."),
            image: qsTr("Your own picture behind everything."),
            video: qsTr("A muted, looping video. It pauses on other pages and stops while the window is hidden or a game is running."),
            panorama: qsTr("The slowly turning view from the Minecraft title screen. It pauses on other pages and stops while the window is hidden or a game is running."),
            shader: qsTr("A calm moving pattern in your theme colors. It pauses on other pages and stops while the window is hidden or a game is running."),
            pattern: qsTr("Blocks in your accent color, different for every instance."),
            solid: qsTr("A calm single color.")
        })[bg.mode] || ""
    }

    // Slideshow interval
    TokenSlider {
        visible: bg.mode === "slideshow"
        label: qsTr("Change every")
        from: 10; to: 600; stepSize: 10
        value: Number(bg.cfg.interval) || 60
        valueText: value < 60 ? qsTr("%1 s").arg(value) : qsTr("%1 min").arg(Math.round(value / 6) / 10)
        onMoved: (v) => bg.set("interval", Math.round(v))
    }

    // Solid color
    Flow {
        visible: bg.mode === "solid"
        Layout.fillWidth: true
        spacing: Style.px(8)
        Repeater {
            model: ["", "#101216", "#1B1F27", "#22182B", "#14231C", "#2A1A14", "#0F1E2E", "#E9E4D8"]
            delegate: AbstractButton {
                id: sw
                width: Style.px(36); height: width
                hoverEnabled: true
                Accessible.name: modelData || qsTr("Theme background")
                onClicked: bg.set("color", modelData)
                background: Rectangle {
                    radius: width / 2
                    color: modelData || Style.bg
                    border.width: (bg.cfg.color || "") === modelData || sw.visualFocus ? 3 : 1
                    border.color: (bg.cfg.color || "") === modelData || sw.visualFocus ? Style.accent : Style.alpha(Style.text, 0.2)
                }
            }
        }
    }

    AppearancePanorama { Layout.fillWidth: true; visible: bg.mode === "panorama" }

    // Animated: preset tiles that move while you point at them.
    Flow {
        id: shaders
        visible: bg.mode === "shader"
        Layout.fillWidth: true
        spacing: Style.px(10)
        readonly property int columns: bg.width > Style.px(420) ? 3 : 2
        readonly property real tileWidth: Math.floor((bg.width - (columns - 1) * spacing) / columns)
        Repeater {
            model: bg.mode === "shader" ? [{ id: "aurora", title: qsTr("Aurora") }, { id: "starfield", title: qsTr("Starfield") },
                    { id: "clouds", title: qsTr("Drifting clouds") }, { id: "plasma", title: qsTr("Soft glow") },
                    { id: "blocks", title: qsTr("Falling blocks") }] : []
            delegate: AbstractButton {
                id: sTile
                readonly property bool on: (bg.cfg.shader || "aurora") === modelData.id
                width: shaders.tileWidth
                height: Math.round(width * 9 / 16)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.title
                Accessible.checkable: true
                Accessible.checked: on
                onClicked: bg.set("shader", modelData.id)
                background: Item {
                    Rectangle { id: sMask; anchors.fill: parent; radius: Style.radius; visible: false; layer.enabled: true }
                    ShaderBackground {
                        anchors.fill: parent
                        preset: modelData.id
                        running: sTile.hovered
                        seed: modelData.id
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: sMask
                            maskThresholdMin: 0.5
                            maskSpreadAtMin: 1.0
                        }
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: sCap.implicitHeight + Style.px(14)
                        radius: Style.radius
                        gradient: Gradient {
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 1; color: Style.alpha(Style.scrim, 0.7) }
                        }
                        Txt {
                            id: sCap
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Style.px(8) }
                            text: modelData.title
                            color: "white"
                            font.pixelSize: Style.fontSmall
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        radius: Style.radius
                        border.width: sTile.on ? Style.px(3) : sTile.visualFocus ? 2 : 1
                        border.color: sTile.on || sTile.visualFocus ? Style.accent : Style.alpha(Style.text, sTile.hovered ? 0.3 : 0.1)
                    }
                }
            }
        }
    }

    // How fast the panorama turns / the pattern moves.
    TokenSlider {
        visible: bg.mode === "panorama" || bg.mode === "shader"
        label: qsTr("Speed")
        from: 0; to: 3; stepSize: 0.1
        value: bg.cfg.speed !== undefined ? Number(bg.cfg.speed) : 1
        valueText: value === 0 ? qsTr("Still") : qsTr("%1×").arg(value.toFixed(1))
        onMoved: (v) => bg.set("speed", Math.round(v * 10) / 10)
    }

    // Gallery — shown for every mode that uses a picture.
    ColumnLayout {
        Layout.fillWidth: true
        visible: bg.mode !== "pattern" && bg.mode !== "solid" && bg.mode !== "panorama" && bg.mode !== "shader"
        spacing: Style.px(10)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Choices {
                Layout.fillWidth: true
                value: bg.filter
                options: [{ value: "all", label: qsTr("All") }, { value: "minecraft", label: qsTr("Landscapes") },
                          { value: "anime", label: qsTr("Anime") }, { value: "mine", label: qsTr("Mine") }]
                onPicked: (v) => bg.filter = v
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Style.px(8)
            QuietButton { text: qsTr("Add image…"); iconName: "image"; onClicked: imageDialog.open() }
            QuietButton { text: qsTr("Add video…"); iconName: "play"; onClicked: videoDialog.open() }
            QuietButton { text: qsTr("Open folder"); iconName: "folder"; plain: true; onClicked: Theme.openThemesFolder() }
        }

        Flow {
            id: grid
            Layout.fillWidth: true
            spacing: Style.px(10)
            readonly property int columns: bg.width > Style.px(420) ? 3 : 2
            readonly property real tileWidth: Math.floor((bg.width - (columns - 1) * spacing) / columns)

            Repeater {
                model: {
                    const mine = Backgrounds.userMedia
                        .filter(m => bg.mode !== "slideshow" || m.kind === "image")
                        .map(m => ({ kind: m.kind, value: m.file, url: m.url, title: m.file, tags: ["mine"] }))
                    const walls = Backgrounds.wallpapers.map(w => ({ kind: "wallpaper", value: w.id, url: w.url, title: w.title, tags: w.tags }))
                    return mine.concat(walls).filter(t => bg.filter === "all" || t.tags.indexOf(bg.filter) >= 0)
                }
                delegate: BgThumb {
                    width: grid.tileWidth
                    height: Math.round(grid.tileWidth * 9 / 16)
                    url: modelData.url
                    title: modelData.title
                    video: modelData.kind === "video"
                    removable: modelData.kind !== "wallpaper"
                    selected: bg.isSelected(modelData)
                    onClicked: bg.choose(modelData)
                    onRemoveRequested: Backgrounds.removeUserFile(modelData.value)
                }
            }
        }
        Txt {
            visible: Backgrounds.wallpapers.length === 0 && Backgrounds.userMedia.length === 0
            text: qsTr("No pictures yet. Add your own image or video.")
            muted: true
        }
    }

    TokenSlider {
        visible: bg.mode !== "solid"
        label: qsTr("Blur"); from: 0; to: 64; stepSize: 1
        value: bg.cfg.blur || 0
        valueText: Math.round(value)
        onMoved: (v) => bg.set("blur", Math.round(v))
    }
    TokenSlider {
        visible: bg.mode !== "solid"
        label: qsTr("Darken"); from: 0; to: 0.9
        value: bg.cfg.dim || 0
        valueText: Math.round(value * 100) + "%"
        onMoved: (v) => bg.set("dim", Math.round(v * 100) / 100)
    }

    // The selected instance can have its own background.
    ColumnLayout {
        Layout.fillWidth: true
        visible: Router.selectedId !== "" && Instances.indexOf(Router.selectedId) >= 0
        spacing: Style.px(8)
        Heading {
            text: qsTr("Background for %1").arg(visible ? (Instances.count, Instances.get(Instances.indexOf(Router.selectedId)).name) : "")
        }
        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            muted: true
            text: qsTr("Shown instead of the theme background while this instance is selected.")
        }
        BgInstancePicker { Layout.fillWidth: true; instanceId: Router.selectedId }
    }

    // Pictures on instance cards and the instance page header.
    Heading { text: qsTr("Instance pictures") }
    SettingsToggle {
        Layout.fillWidth: true
        label: qsTr("Show pictures on instance cards")
        checked: Backgrounds.coversEnabled
        onToggled: (on) => Backgrounds.coversEnabled = on
    }
    Txt {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        muted: true
        text: qsTr("Modpacks show their own picture from Modrinth or CurseForge; other instances show their newest screenshot.")
    }

    function isSelected(t) {
        if (mode === "slideshow")
            return t.kind !== "video" && (slideIds.length === 0 ? t.kind === "wallpaper" : slideIds.indexOf(t.value) >= 0)
        if (t.kind === "wallpaper") return (mode === "wallpaper" || mode === "screenshot") && t.value === wallpaper
        if (t.kind === "image") return mode === "image" && t.value === cfg.image
        return mode === "video" && t.value === cfg.video
    }
    function choose(t) {
        if (mode === "slideshow") {
            if (t.kind === "video") return
            // An empty list means "all wallpapers": start from that when the user first unticks one.
            let ids = slideIds.length ? slideIds.slice() : Backgrounds.wallpapers.map(w => w.id)
            const i = ids.indexOf(t.value)
            if (i >= 0) ids.splice(i, 1); else ids.push(t.value)
            set("slideshow", ids)
            return
        }
        if (t.kind === "wallpaper") {
            set("wallpaper", t.value)
            // Screenshot mode keeps showing screenshots; the wallpaper becomes its fallback.
            if (mode !== "screenshot") set("mode", "wallpaper")
        } else {
            set(t.kind, t.value)
            set("mode", t.kind)
        }
    }
    function importAndUse(url, kind) {
        const f = Backgrounds.importFile(url)
        if (!f) { Router.toast(qsTr("Couldn't add that file."), true); return }
        set(kind, f)
        set("mode", kind)
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Choose a background image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: bg.importAndUse(selectedFile, "image")
    }
    FileDialog {
        id: videoDialog
        title: qsTr("Choose a background video")
        nameFilters: [qsTr("Videos (*.mp4 *.webm *.mov *.m4v)")]
        onAccepted: bg.importAndUse(selectedFile, "video")
    }
}
