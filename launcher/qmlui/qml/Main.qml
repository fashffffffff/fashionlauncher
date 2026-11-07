import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "JavaPresets.js" as JP

ApplicationWindow {
    id: win
    width: 1240
    height: 760
    minimumWidth: 980
    minimumHeight: 620
    visible: false
    title: Launcher.appName
    color: Style.bg
    font.family: Style.ui

    readonly property var pageFiles: ({
        home: "PageHome.qml", instances: "PageInstances.qml", browse: "PageBrowse.qml",
        appearance: "PageAppearance.qml", settings: "PageSettings.qml",
        instance: "PageInstance.qml", create: "PageCreate.qml", version: "PageVersion.qml",
        friends: "PageFriends.qml", skins: "PageSkins.qml", gallery: "PageGallery.qml",
        receive: "PageReceivePack.qml"
    })

    // Debug/automation entry (FFPRISM_PAGE): a section name, "create", "accounts", or "instance:<id>[:<tab>]"
    function showPage(name: var) {
        if (name === "accounts") accounts.open()
        else if (name === "palette") cmdPalette.open()
        else if (name.startsWith("version:")) Router.open("version", { instanceId: name.slice(8) })
        else if (name.startsWith("receive:")) Router.open("receive", { file: name.slice(8) })
        else if (name.startsWith("share:")) {
            Router.openInstance(name.slice(6))
            Qt.createComponent("ShareDialog.qml").createObject(win, { instanceId: name.slice(6), destroyOnClose: true }).open()
        }
        else if (name.startsWith("instance:")) { const p = name.split(":"); Router.openInstance(p[1], p[2]) }
        else if (name.startsWith("dialog:")) { const d = name.split(":"); Launcher.dialogRequested(d[1], d[2]) }
        else if (name === "java") Settings.downloadJava("")
        // Crash doctor on a log file: crash:<instanceId>:<path>
        else if (name.startsWith("crash:")) { const c = name.split(":"); if (Doctor.analyzeFile(c[1], c.slice(2).join(":"))) Launcher.dialogRequested("crash", c[1]) }
        else if (name.startsWith("launch:")) Launcher.launch(name.slice(7))
        // Preview of a launch question (answers go nowhere): prompt:ask | prompt:account | prompt:name
        else if (name.startsWith("prompt:")) Prompts.requested({ id: -1, kind: name.slice(7), title: "Preview", name: "Player",
            text: "The Minecraft authentication servers are currently unavailable, launching in offline mode.",
            buttons: [qsTr("Kill"), qsTr("Cancel")], tone: "danger" })
        else if (pageFiles[name]) Router.go(name)
    }

    Connections {
        target: Router
        function onGoRequested(page) {
            Router.section = page
            stack.replace(null, win.pageFiles[page], { params: {} })
        }
        function onOpenRequested(page, params) { stack.push(win.pageFiles[page], { params: params }) }
        function onBackRequested() { if (stack.depth > 1) stack.pop() }
        function onToastRequested(message, isError) { toastBox.show(message, isError) }
        function onAccountsRequested() { accounts.open() }
    }
    Connections {
        target: Launcher
        function onLaunchFailed(name, reason) { Router.toast(qsTr("%1 didn't start: %2").arg(name).arg(reason), true) }
        function onNotify(message) { Router.toast(message, false) }
    }
    Connections {
        target: Accounts
        function onSignedIn(name) { Router.toast(qsTr("Signed in as %1").arg(name), false) }
    }

    // New players get the balanced Java preset once; anyone who already set arguments keeps them.
    Component.onCompleted: {
        if (!Settings.get("QmlJvmPresetApplied")) {
            if (!Settings.get("JvmArgs")) Settings.set("JvmArgs", JP.argsFor("balanced"))
            Settings.set("QmlJvmPresetApplied", true)
        }
    }

    Shortcut { sequences: [StandardKey.Back, "Esc"]; enabled: stack.depth > 1; onActivated: Router.back() }
    Shortcut { sequences: [StandardKey.Find]; onActivated: Router.go("instances") }

    Backdrop {
        id: backdrop
        anchors.fill: parent
        readonly property var cur: { Instances.count; const i = Instances.indexOf(Router.selectedId); return i >= 0 ? Instances.get(i) : null }
        screenshot: cur ? cur.backdrop : ""
        seed: cur ? cur.instanceId : "empty"
        onImageSourceChanged: Theme.setAccentImage(imageSource)
        Component.onCompleted: Theme.setAccentImage(imageSource)
    }

    // The scene shows fully on Home; every other page sits on a calmer, darker backdrop.
    Rectangle {
        anchors.fill: parent
        color: Style.bg
        opacity: Router.section === "home" && stack.depth === 1 ? 0 : 0.8
        Behavior on opacity { NumberAnimation { duration: Style.dur(220); easing.type: Easing.OutCubic } }
    }

    // Drop a modpack or exported instance anywhere to import it. Declared under the pages so their own drop
    // zones (e.g. resource packs on the mods tab) win; this one only catches drops nobody else takes.
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onEntered: (d) => d.accepted = d.urls.length === 1 && /\.(zip|mrpack)$/i.test(String(d.urls[0]))
        onDropped: (d) => { if (d.urls.length) Router.open("create", { source: "import", url: String(d.urls[0]) }) }
    }

    RowLayout {
        anchors { fill: parent; margins: Style.px(12) }
        spacing: Style.px(12)
        layoutDirection: Style.navPosition === "right" ? Qt.RightToLeft : Qt.LeftToRight

        Sidebar { Layout.fillHeight: true; Layout.preferredWidth: width }

        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            // leave a strip for the running-tasks pill so it never covers page controls
            Layout.bottomMargin: Tasks.busy ? Style.px(60) : 0
            Behavior on Layout.bottomMargin { NumberAnimation { duration: Style.dur(200) } }
            clip: true
            initialItem: "PageHome.qml"
            // One quiet transition: content slides a little and fades, so you see where you went.
            pushEnter: Transition {
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.dur(200) }
                    NumberAnimation { property: "x"; from: Style.px(24); to: 0; duration: Style.dur(240); easing.type: Easing.OutCubic }
                }
            }
            pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.dur(150) } }
            popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.dur(200) } }
            popExit: Transition {
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.dur(150) }
                    NumberAnimation { property: "x"; from: 0; to: Style.px(24); duration: Style.dur(180) }
                }
            }
            replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.dur(180) } }
            replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.dur(120) } }
        }
    }

    Rectangle {
        anchors { fill: parent; margins: Style.px(12) }
        visible: drop.containsDrag
        z: 50
        radius: Style.radius + Style.px(6)
        color: Style.alpha(Style.scrim, 0.6)
        border.width: 3
        border.color: Style.accent
        Column {
            anchors.centerIn: parent
            spacing: Style.px(10)
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "export"; size: Style.px(40); color: Style.accent; rotation: 180 }
            Text {
                text: qsTr("Drop to import this modpack")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.fontTitle
            }
        }
    }

    AccountsPanel { id: accounts }
    CommandPalette { id: cmdPalette }
    ToolsLaunchCheck {}   // pre-launch checks dialog + Tools notices
    Shortcut { sequences: ["Ctrl+K"]; onActivated: cmdPalette.opened ? cmdPalette.close() : cmdPalette.open() }
    Connections { target: Router; function onPaletteRequested() { cmdPalette.open() } }

    // Installs, mod downloads and updates stay visible on every page.
    TaskIndicator {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Style.px(14) }
        z: 60
        onFinished: (message, error) => toastBox.show(message, error)
    }

    Onboarding { id: onboarding; anchors.fill: parent }

    // Dialogs and launch questions (copy, export, delete, icon, account, player name, progress)
    DialogHost { window: win }

    // Toasts
    Rectangle {
        id: toastBox
        property bool isError: false
        function show(message, error) { toastText.text = message; isError = error; opacity = 1; hideTimer.restart() }
        anchors { right: parent.right; bottom: parent.bottom; margins: Style.px(28) }
        width: Math.min(Style.px(420), toastText.implicitWidth + Style.px(32))
        height: toastText.implicitHeight + Style.px(24)
        radius: Style.radius
        color: Style.surface
        border.width: 1
        border.color: isError ? Style.danger : Style.line
        opacity: 0
        visible: opacity > 0
        z: 100
        Behavior on opacity { NumberAnimation { duration: Style.dur(180) } }
        Txt {
            id: toastText
            anchors { fill: parent; margins: Style.px(12); leftMargin: Style.px(16); rightMargin: Style.px(16) }
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Timer { id: hideTimer; interval: toastBox.isError ? 8000 : 3500; onTriggered: toastBox.opacity = 0 }
        TapHandler { onTapped: toastBox.opacity = 0 }
    }
}
