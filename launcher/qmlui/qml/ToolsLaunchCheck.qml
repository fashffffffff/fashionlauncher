import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pre-launch checks: when Tools finds something that will certainly stop the game, the launch waits here.
// Also shows Tools' notices (e.g. "This modpack set 8 GB of memory") as toasts.
Item {
    id: host

    Connections {
        target: Tools
        function onLaunchBlocked(instanceId, name, offline, problems) { dialog.show(instanceId, name, offline, problems) }
        function onNotice(message) { Router.toast(message, false) }
    }

    // Snapshot automation: show the "From another launcher" import
    Timer {
        running: Tools.automation === "launchers" || Tools.automation.startsWith("folder:") || Tools.automation.startsWith("import:")
        interval: 800
        onTriggered: Router.open("create", { source: "import" })
    }

    function kindIcon(kind) {
        return kind === "missing" ? "plus" : kind === "duplicate" ? "copy" : kind === "minecraft" ? "cube"
             : kind === "java" ? "terminal" : "sliders"
    }

    Popup {
        id: dialog
        property string instanceId
        property string name
        property bool offline: false
        property var problems: []
        readonly property bool allAuto: problems.every(p => p.autoFix)

        function show(id, n, off, list) { instanceId = id; name = n; offline = off; problems = list; open() }
        function play() { close(); Tools.allowOnce(instanceId); Launcher.launch(instanceId, offline) }
        function recheck() {
            problems = Tools.check(instanceId)
            if (problems.length === 0) {
                close()
                Router.toast(qsTr("All fixed"), false)
                Launcher.launch(instanceId, offline)
            }
        }
        function runFix(p) {
            if (p.fixAction === "disable") {
                const n = Tools.disableFiles(p.fixArg)
                if (n > 0) Router.toast(qsTr("Turned off %n file(s)", "", n), false)
                recheck()
            } else if (p.fixAction === "find") {
                close()
                Tools.requestFind(instanceId, p.fixArg || "")
                Router.openInstance(instanceId, "mods")
            } else if (p.fixAction === "settings") {
                close()
                Router.openInstance(instanceId, "settings")
            }
        }
        // "Fix": everything that needs no choice, then the first thing that does
        function fixAll() {
            const n = Tools.autoFix(problems)
            if (n > 0) Router.toast(qsTr("Turned off %n file(s)", "", n), false)
            const left = Tools.check(instanceId)
            if (left.length === 0) { problems = left; recheck(); return }
            problems = left
            const manual = left.find(p => !p.autoFix)
            if (manual) runFix(manual)
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Style.px(560), parent ? parent.width - Style.px(48) : Style.px(560))
        modal: true
        focus: true
        padding: Style.px(24)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }

        contentItem: ColumnLayout {
            spacing: Style.px(16)

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.px(4)
                Text {
                    Layout.fillWidth: true
                    text: qsTr("“%1” won't start like this").arg(dialog.name)
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Txt {
                    Layout.fillWidth: true
                    muted: true
                    wrapMode: Text.WordWrap
                    text: qsTr("%n problem(s) will stop the game while it loads.", "", dialog.problems.length)
                }
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Style.px(340))
                clip: true
                spacing: Style.px(8)
                model: dialog.problems
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width - (list.contentHeight > list.height ? Style.px(10) : 0)
                    height: row.implicitHeight + Style.px(24)
                    radius: Style.radius
                    color: Style.panel(0)
                    border.width: 1
                    border.color: Style.panelBorder
                    RowLayout {
                        id: row
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: Style.px(14); rightMargin: Style.px(10) }
                        spacing: Style.px(12)
                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            width: Style.px(32); height: width; radius: width / 2
                            color: Style.alpha(Style.danger, 0.14)
                            Icon { anchors.centerIn: parent; name: host.kindIcon(modelData.kind); size: Style.px(16); color: Style.danger }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(2)
                            Txt { Layout.fillWidth: true; text: modelData.title; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                            Txt { Layout.fillWidth: true; text: modelData.detail; muted: true; font.pixelSize: Style.fontSmall; wrapMode: Text.WordWrap; maximumLineCount: 4; elide: Text.ElideRight }
                        }
                        QuietButton {
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.fixLabel
                            onClicked: dialog.runFix(modelData)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(10)
                ResSwitch {
                    id: keepChecking
                    checked: true
                    Accessible.name: qsTr("Check mods before playing")
                    onToggled: Tools.checksEnabled = checked
                }
                Txt {
                    Layout.fillWidth: true
                    text: keepChecking.checked ? qsTr("Check mods before playing") : qsTr("Won't check again. You can turn it back on in the Mods tab.")
                    muted: true
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.WordWrap
                    TapHandler { onTapped: keepChecking.toggle() }
                }
                QuietButton { text: qsTr("Play anyway"); onClicked: dialog.play() }
                PrimaryButton {
                    implicitHeight: Style.px(40)
                    implicitWidth: Math.max(Style.px(110), implicitContentWidth + Style.px(40))
                    text: dialog.allAuto ? qsTr("Fix and play") : qsTr("Fix")
                    iconName: "check"
                    onClicked: dialog.fixAll()
                }
            }
        }
        onOpened: keepChecking.checked = Tools.checksEnabled
    }
}
