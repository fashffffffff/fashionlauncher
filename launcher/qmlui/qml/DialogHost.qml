import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every dialog the launcher itself asks for: launch questions (Prompts), the progress of waits and
// jobs, and the instance dialogs Launcher.copyInstance()/exportInstance()/deleteInstance()/changeIcon()
// request, and the Java download (Settings.downloadJava). Lives once in Main.qml, so no classic window is ever needed for these.
Item {
    id: host
    anchors.fill: parent
    property var window: null

    // ---- routing of Launcher requests
    Connections {
        target: Launcher
        function onDialogRequested(kind, instanceId) {
            if (kind === "copy") copyDlg.openFor(instanceId)
            else if (kind === "export") exportDlg.openFor(instanceId)
            else if (kind === "delete") host.confirmDelete(instanceId)
            else if (kind === "icon") host.pickIcon(instanceId)
            else if (kind === "crash") crashDlg.open()
        }
        function onInstancePageRequested(instanceId, tab) { host.raiseWindow(); Router.openInstance(instanceId, tab) }
        function onSettingsRequested(page) {
            host.raiseWindow()
            if (page === "accounts") Router.openAccounts()
            else Router.go("settings")
        }
        function onCreateRequested() { Router.open("create") }
    }

    function raiseWindow() {
        if (!window) return
        if (window.visibility === Window.Minimized || !window.visible) window.showNormal()
        window.raise()
        window.requestActivate()
    }

    function instanceName(id) {
        const i = Instances.indexOf(id)
        return i >= 0 ? Instances.get(i).name : ""
    }

    // ---- delete
    function confirmDelete(id) {
        const problem = InstanceActions.deleteProblem(id)
        if (problem) { confirm.ask({ title: qsTr("Can't delete this instance"), text: problem, tone: "warning" }); return }
        const linked = InstanceActions.linkedInstances(id)
        let text = qsTr("“%1” and everything in it — worlds, mods, screenshots — will be moved to the Trash.").arg(instanceName(id))
        if (linked.length)
            text += "\n\n" + qsTr("These instances share files with it and may break: %1").arg(linked.join(", "))
        confirm.ask({ title: qsTr("Delete “%1”?").arg(instanceName(id)), text: text,
                      buttons: [qsTr("Delete"), qsTr("Cancel")], tone: "danger" }, (i) => {
            if (i !== 0) return
            const name = instanceName(id)
            const trashed = InstanceActions.remove(id)
            Router.toast(trashed ? qsTr("“%1” was moved to the Trash.").arg(name) : qsTr("“%1” was deleted.").arg(name), false)
        })
    }

    // ---- icon
    property string iconFor: ""
    function pickIcon(id) {
        iconFor = id
        iconPicker.currentKey = InstanceActions.iconKey(id)
        iconPicker.open()
    }
    CreateIconPicker {
        id: iconPicker
        parent: Overlay.overlay
        anchors.centerIn: parent
        onChosen: (key) => { if (key) InstanceActions.setIcon(host.iconFor, key) }
    }

    CopyInstanceDialog { id: copyDlg }
    GameOptionsDialog { id: gameOptionsDlg }
    Connections {
        target: Router
        function onGameOptionsRequested(params) { gameOptionsDlg.openFor(params) }
    }
    // ---- crash doctor: after a game crash (also when the window was closed while playing), or from the log tab
    CrashDoctorDialog { id: crashDlg }
    Connections {
        target: Doctor
        function onCrashed(instanceId) { if (Doctor.takePending()) { host.raiseWindow(); crashDlg.open() } }
    }
    Component.onCompleted: if (Doctor.pendingInstance) Qt.callLater(() => { if (Doctor.takePending()) crashDlg.open() })
    JavaInstallDialog { id: javaDlg }
    Connections {
        target: Settings
        function onJavaDownloadRequested(instanceId) { host.raiseWindow(); javaDlg.openFor(instanceId) }
    }
    ExportDialog { id: exportDlg }
    QuickPlayDialog { id: quickPlayDlg }
    Connections {
        target: Router
        function onQuickPlayRequested(instanceId) { quickPlayDlg.openFor(instanceId) }
    }

    // ---- generic question, also used for Prompts "ask"
    ConfirmDialog { id: confirm }

    // ---- launch questions; one at a time, in order
    property var queue: []
    property var current: null
    Connections {
        target: Prompts
        function onRequested(request) {
            host.queue = host.queue.concat([request])
            host.raiseWindow()
            host.next()
        }
    }
    function next() {
        if (current || !queue.length) return
        current = queue[0]
        queue = queue.slice(1)
        const r = current
        if (r.kind === "ask") {
            launchAsk.ask({ title: r.title, text: r.text, buttons: r.buttons, tone: r.tone }, (i) => host.reply(r, i))
        } else if (r.kind === "account") {
            accountDlg.request = r
            accountDlg.open()
        } else if (r.kind === "name") {
            nameDlg.request = r
            nameField.text = r.name || ""
            nameDlg.open()
        } else {
            host.reply(r, undefined)
        }
    }
    function reply(r, value) {
        Prompts.answer(r.id, value)
        current = null
        Qt.callLater(next)
    }

    ConfirmDialog { id: launchAsk }

    ModalCard {
        id: accountDlg
        property var request: null
        property var picked: undefined
        title: request ? request.title : ""
        subtitle: qsTr("You can change the account for every launch in the accounts panel.")
        onAboutToShow: { picked = undefined; useDefault.checked = false }
        onClosed: { const r = request; request = null; host.reply(r, picked) }

        ListView {
            id: accList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Style.px(300))
            clip: true
            spacing: Style.px(8)
            model: Accounts
            delegate: AbstractButton {
                id: accRow
                width: accList.width
                height: Style.px(56)
                hoverEnabled: true
                Accessible.name: model.name
                onClicked: { accountDlg.picked = { accountId: model.accountId, useAsDefault: useDefault.checked }; accountDlg.close() }
                background: Rectangle {
                    radius: Style.radius
                    color: accRow.hovered ? Style.surfaceHi : Style.alpha(Style.surfaceHi, 0.5)
                    border.width: accRow.visualFocus ? 2 : 0
                    border.color: Style.accent
                }
                contentItem: RowLayout {
                    anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                    spacing: Style.px(12)
                    Image {
                        source: model.face
                        Layout.preferredWidth: Style.px(32); Layout.preferredHeight: Style.px(32)
                        sourceSize: Qt.size(64, 64)
                        smooth: false
                    }
                    Txt { text: model.name; font.weight: Font.Medium; Layout.fillWidth: true }
                    Txt { text: model.kind === "msa" ? qsTr("Microsoft") : qsTr("Offline"); muted: true; font.pixelSize: Style.fontSmall }
                }
            }
        }
        SettingsToggle { id: useDefault; label: qsTr("Always use this account"); onToggled: (on) => checked = on }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            QuietButton { text: qsTr("Cancel"); onClicked: accountDlg.close() }
        }
    }

    ModalCard {
        id: nameDlg
        property var request: null
        property var value: undefined
        title: request ? request.title : ""
        subtitle: request ? request.text : ""
        onAboutToShow: value = undefined
        onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }
        onClosed: { const r = request; request = null; host.reply(r, value) }
        function accept() { if (nameField.acceptableInput && nameField.text.length) { value = nameField.text; close() } }

        Field {
            id: nameField
            Layout.fillWidth: true
            iconName: "user"
            placeholderText: qsTr("Player name")
            // Minecraft names: 3–16 letters, digits and underscores
            validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9_]{1,16}/ }
            onAccepted: nameDlg.accept()
        }
        Txt {
            Layout.fillWidth: true
            text: qsTr("Letters, digits and _ only, up to 16 characters.")
            muted: true
            font.pixelSize: Style.fontSmall
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Style.px(8)
            QuietButton { text: qsTr("Cancel"); onClicked: nameDlg.close() }
            PrimaryButton { text: qsTr("Play"); iconName: "play"; implicitHeight: Style.px(40); enabled: nameField.text.length > 0; onClicked: nameDlg.accept() }
        }
    }

    // ---- progress of something the launcher waits for (account refresh) or a job (copy, export)
    ModalCard {
        id: progressDlg
        readonly property var t: Prompts.task.active ? Prompts.task : InstanceActions.job
        readonly property bool fromPrompts: !!Prompts.task.active
        visible: !!(t && t.active)
        dismissable: false
        title: t && t.title ? t.title : ""
        subtitle: t && t.status ? t.status : qsTr("Working…")

        ProgressBar {
            id: bar
            Layout.fillWidth: true
            Layout.topMargin: Style.px(4)
            from: 0; to: 1
            value: progressDlg.t && progressDlg.t.progress >= 0 ? progressDlg.t.progress : 0
            indeterminate: !progressDlg.t || !(progressDlg.t.progress >= 0)
            background: Rectangle { implicitHeight: Style.px(6); radius: 3; color: Style.line }
            contentItem: Item {
                implicitHeight: Style.px(6)
                Rectangle {
                    width: bar.indeterminate ? parent.width * 0.3 : bar.visualPosition * parent.width
                    height: parent.height
                    radius: 3
                    color: Style.accent
                    Behavior on width { enabled: !bar.indeterminate; NumberAnimation { duration: Style.dur(160) } }
                    SequentialAnimation on x {
                        running: bar.indeterminate && progressDlg.visible && Style.motion
                        loops: Animation.Infinite
                        NumberAnimation { from: 0; to: bar.width * 0.7; duration: 900; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: bar.width * 0.7; to: 0; duration: 900; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            QuietButton {
                text: qsTr("Cancel")
                onClicked: progressDlg.fromPrompts ? Prompts.cancelTask() : InstanceActions.cancelJob()
            }
        }
    }

    Connections {
        target: InstanceActions
        function onJobFinished(ok, message) { Router.toast(message, !ok) }
    }
}
