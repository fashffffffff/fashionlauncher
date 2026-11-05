import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 720
    minimumWidth: 900
    minimumHeight: 580
    visible: false
    title: Launcher.appName
    color: Style.bg
    font.family: Style.ui

    // Selection is tracked by id, so sorting and filtering never jump it to another instance.
    property string selectedId: ""
    property var current: null
    property int selectedIndex: -1
    property string pageName: "play"

    function refreshCurrent() {
        let i = Instances.indexOf(selectedId)
        if (i < 0 && Instances.count > 0 && Instances.filter === "") {
            i = 0
            selectedId = Instances.get(0).instanceId
        }
        selectedIndex = i
        current = i >= 0 ? Instances.get(i) : null
    }
    function selectIndex(i) {
        if (i < 0 || i >= Instances.count) return
        selectedId = Instances.get(i).instanceId
        refreshCurrent()
    }
    function showPage(name: var) {
        if (name === "accounts") { accounts.open(); return }
        if (name !== "library" && Instances.filter !== "") Instances.filter = ""
        pageName = name
        if (name === "library") library.focusSearch()
        else stage.forceActiveFocus()
    }
    function toast(message, isError) { toastBox.show(message, isError) }

    Connections {
        target: Instances
        function onCountChanged() { win.refreshCurrent() }
        function onDataChanged() { win.refreshCurrent() }
        function onLayoutChanged() { win.refreshCurrent() }
        function onModelReset() { win.refreshCurrent() }
    }
    Connections {
        target: Launcher
        function onLaunchFailed(name, reason) { win.toast(qsTr("%1 didn't start: %2").arg(name).arg(reason), true) }
        function onNotify(message) { win.toast(message, false) }
    }
    Connections {
        target: Accounts
        function onSignedIn(name) { win.toast(qsTr("Signed in as %1").arg(name), false) }
    }
    Component.onCompleted: refreshCurrent()

    Backdrop {
        anchors.fill: parent
        screenshot: win.current ? win.current.backdrop : ""
        seed: win.current ? win.current.instanceId : "empty"
    }

    // Calm the scene behind list-like pages; the Play page shows it in full.
    Rectangle {
        anchors.fill: parent
        color: Style.bg
        opacity: win.pageName === "play" ? 0 : 0.86
        Behavior on opacity { NumberAnimation { duration: Style.dur(220); easing.type: Easing.OutCubic } }
    }
    // Keep the top bar readable over bright screenshots.
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Style.px(140)
        gradient: Gradient {
            GradientStop { position: 0.0; color: Style.alpha(Style.scrim, 0.6) }
            GradientStop { position: 1.0; color: Style.alpha(Style.scrim, 0.0) }
        }
    }

    // Keyboard: 1–9 pick a hotbar slot, Enter plays, arrows step through.
    FocusScope {
        id: stage
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            if (win.pageName !== "play") return
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) { win.selectIndex(event.key - Qt.Key_1); event.accepted = true }
            else if (event.key === Qt.Key_Right) { win.selectIndex(Math.min(Instances.count - 1, win.selectedIndex + 1)); event.accepted = true }
            else if (event.key === Qt.Key_Left) { win.selectIndex(Math.max(0, win.selectedIndex - 1)); event.accepted = true }
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && win.current) {
                Launcher.launch(win.current.instanceId); event.accepted = true
            }
        }

        StackLayout {
            anchors { top: topBar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            currentIndex: ["play", "library", "appearance"].indexOf(win.pageName)
            PlayPage {
                current: win.current
                selectedIndex: win.selectedIndex
                onSelect: (i) => win.selectIndex(i)
                onOpenLibrary: win.showPage("library")
            }
            LibraryPage {
                id: library
                selectedIndex: win.selectedIndex
                onPick: (i) => {
                    win.selectedId = Instances.get(i).instanceId
                    win.showPage("play")
                }
            }
            AppearancePage {}
        }

        // Top bar
        Item {
            id: topBar
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: Style.px(64)

            Text {
                anchors { left: parent.left; leftMargin: Style.px(56); verticalCenter: parent.verticalCenter }
                text: Launcher.appName
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(15)
                font.weight: Font.DemiBold
            }

            Row {
                anchors.centerIn: parent
                spacing: Style.px(4)
                Repeater {
                    model: [{ id: "play", label: qsTr("Play") }, { id: "library", label: qsTr("Library") },
                            { id: "appearance", label: qsTr("Appearance") }]
                    delegate: AbstractButton {
                        id: tab
                        readonly property bool on: win.pageName === modelData.id
                        implicitWidth: tabText.implicitWidth + Style.px(28)
                        implicitHeight: Style.px(36)
                        hoverEnabled: true
                        Accessible.name: modelData.label
                        onClicked: win.showPage(modelData.id)
                        background: Rectangle {
                            radius: height / 2
                            color: tab.on ? Style.alpha(Style.text, 0.14) : tab.hovered ? Style.alpha(Style.text, 0.07) : "transparent"
                            border.width: tab.visualFocus ? 2 : 0
                            border.color: Style.accent
                        }
                        contentItem: Text {
                            id: tabText
                            text: modelData.label
                            color: tab.on ? Style.text : Style.textMuted
                            font.family: Style.ui
                            font.pixelSize: Style.fontBody
                            font.weight: tab.on ? Font.DemiBold : Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            RowLayout {
                anchors { right: parent.right; rightMargin: Style.px(56); verticalCenter: parent.verticalCenter }
                spacing: Style.px(8)
                AbstractButton {
                    id: accountChip
                    implicitHeight: Style.px(40)
                    implicitWidth: chipRow.implicitWidth + Style.px(20)
                    hoverEnabled: true
                    Accessible.name: qsTr("Accounts")
                    onClicked: accounts.open()
                    background: Rectangle {
                        radius: height / 2
                        color: accountChip.hovered ? Style.alpha(Style.text, 0.12) : Style.alpha(Style.surface, 0.7)
                        border.width: accountChip.visualFocus ? 2 : 1
                        border.color: accountChip.visualFocus ? Style.accent : Style.alpha(Style.text, 0.08)
                    }
                    contentItem: Item {
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: Style.px(8)
                            Image {
                                visible: !!Accounts.current.face
                                source: Accounts.current.face || ""
                                width: Style.px(24); height: width
                                sourceSize: Qt.size(48, 48)
                                smooth: false
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Icon { visible: !Accounts.current.face; name: "user"; size: Style.px(18); anchors.verticalCenter: parent.verticalCenter }
                            Txt {
                                text: Accounts.current.name || qsTr("Add account")
                                font.weight: Font.Medium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
                QuietButton {
                    iconName: "sliders"
                    tip: qsTr("Launcher settings")
                    onClicked: settingsMenu.popup()
                    ThemedMenu {
                        id: settingsMenu
                        ThemedMenuItem { text: qsTr("Settings"); iconName: "sliders"; onTriggered: Launcher.openSettings("") }
                        ThemedMenuItem { text: qsTr("Java"); iconName: "cube"; onTriggered: Launcher.openSettings("java-settings") }
                        ThemedMenuItem { text: qsTr("Classic window"); iconName: "grid"; onTriggered: Launcher.openClassicWindow() }
                    }
                }
            }
        }
    }

    AccountsPanel { id: accounts }

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
