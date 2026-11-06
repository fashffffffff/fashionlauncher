import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
        instance: "PageInstance.qml", create: "PageCreate.qml"
    })

    // Debug/automation entry (FFPRISM_PAGE): a section name, "accounts", or "instance:<id>"
    function showPage(name: var) {
        if (name === "accounts") accounts.open()
        else if (name.startsWith("instance:")) Router.openInstance(name.slice(9))
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

    Shortcut { sequences: [StandardKey.Back, "Esc"]; enabled: stack.depth > 1; onActivated: Router.back() }
    Shortcut { sequences: [StandardKey.Find]; onActivated: Router.go("instances") }

    Backdrop {
        id: backdrop
        anchors.fill: parent
        readonly property var cur: { Instances.count; const i = Instances.indexOf(Router.selectedId); return i >= 0 ? Instances.get(i) : null }
        screenshot: cur ? cur.backdrop : ""
        seed: cur ? cur.instanceId : "empty"
    }

    // The scene shows fully on Home; every other page sits on a calmer, darker backdrop.
    Rectangle {
        anchors.fill: parent
        color: Style.bg
        opacity: Router.section === "home" && stack.depth === 1 ? 0 : 0.8
        Behavior on opacity { NumberAnimation { duration: Style.dur(220); easing.type: Easing.OutCubic } }
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

    AccountsPanel { id: accounts }

    Onboarding { id: onboarding; anchors.fill: parent }

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
