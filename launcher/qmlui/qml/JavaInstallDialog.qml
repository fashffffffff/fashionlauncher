import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Download a Java runtime: provider, major version (recommended ones marked), then a build.
// The download continues in the background and shows in the global task indicator.
ModalCard {
    id: dlg
    property string instanceId
    property string provider: "net.minecraft.java"
    property string major: ""
    property int build: -1
    width: Math.min(parent ? parent.width - Style.px(48) : 720, Style.px(720))
    height: Math.min(parent ? parent.height - Style.px(48) : 560, Style.px(560))
    title: qsTr("Download Java")
    subtitle: qsTr("Minecraft needs Java to run. Recommended versions fit this game; the newest build is picked for you.")

    function openFor(id) {
        instanceId = id || ""
        pick("net.minecraft.java")
        open()
    }
    function pick(uid) {
        provider = uid
        major = ""
        build = -1
        JavaInstaller.loadMajors(uid, instanceId)
        autoPick()       // the list may already be loaded
    }
    function autoPick() {
        if (major || !JavaInstaller.majors.length) return
        const rec = JavaInstaller.majors.find(m => m.recommended) || JavaInstaller.majors[0]
        major = rec.id
        JavaInstaller.loadBuilds(rec.id)
    }
    // Pick the first recommended major once the list is in, then its newest build.
    Connections {
        target: JavaInstaller
        function onMajorsChanged() { if (dlg.visible) dlg.autoPick() }
        function onBuildsChanged() { dlg.build = JavaInstaller.builds.length ? 0 : -1 }
        function onInstallFinished(ok, message) {
            Router.toast(message, !ok)
            if (ok) Settings.rescanJava()
        }
    }

    CreateSegmented {
        options: JavaInstaller.providers().map(p => ({ value: p.uid, label: p.name }))
        value: dlg.provider
        onPicked: (v) => dlg.pick(v)
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Style.px(12)

        // Major versions
        Rectangle {
            Layout.preferredWidth: Style.px(190)
            Layout.fillHeight: true
            radius: Style.radius
            color: Style.alpha(Style.bg, 0.35)
            border.width: 1
            border.color: Style.line
            ListView {
                id: majors
                anchors { fill: parent; margins: Style.px(6) }
                clip: true
                spacing: Style.px(2)
                model: JavaInstaller.majors
                ScrollBar.vertical: ScrollBar {}
                delegate: AbstractButton {
                    id: mRow
                    width: majors.width
                    height: Style.px(38)
                    hoverEnabled: true
                    readonly property bool chosen: dlg.major === modelData.id
                    Accessible.name: modelData.label
                    onClicked: { dlg.major = modelData.id; JavaInstaller.loadBuilds(modelData.id) }
                    background: Rectangle {
                        radius: Style.radius - 4
                        color: mRow.chosen ? Style.alpha(Style.accent, 0.2) : mRow.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                    }
                    contentItem: RowLayout {
                        anchors { fill: parent; leftMargin: Style.px(10); rightMargin: Style.px(8) }
                        Txt { text: modelData.label; Layout.fillWidth: true; font.weight: mRow.chosen ? Font.DemiBold : Font.Normal }
                        Rectangle {
                            visible: modelData.recommended
                            radius: height / 2
                            implicitHeight: Style.px(20)
                            implicitWidth: recText.implicitWidth + Style.px(14)
                            color: Style.alpha(Style.success, 0.18)
                            Txt { id: recText; anchors.centerIn: parent; text: qsTr("fits"); color: Style.success; font.pixelSize: Style.fontSmall }
                        }
                    }
                }
            }
        }

        // Builds of the chosen major
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radius
            color: Style.alpha(Style.bg, 0.35)
            border.width: 1
            border.color: Style.line
            ListView {
                id: builds
                anchors { fill: parent; margins: Style.px(6) }
                clip: true
                spacing: Style.px(2)
                model: JavaInstaller.builds
                ScrollBar.vertical: ScrollBar {}
                delegate: AbstractButton {
                    id: bRow
                    width: builds.width
                    height: Style.px(48)
                    hoverEnabled: true
                    readonly property bool chosen: dlg.build === modelData.index
                    Accessible.name: modelData.name
                    onClicked: dlg.build = modelData.index
                    onDoubleClicked: { dlg.build = modelData.index; dlg.start() }
                    background: Rectangle {
                        radius: Style.radius - 4
                        color: bRow.chosen ? Style.alpha(Style.accent, 0.2) : bRow.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                        border.width: bRow.chosen ? 1 : 0
                        border.color: Style.accent
                    }
                    contentItem: ColumnLayout {
                        anchors { fill: parent; leftMargin: Style.px(10); rightMargin: Style.px(10) }
                        spacing: 0
                        Txt { text: modelData.version + "  ·  " + modelData.type; font.weight: Font.Medium; Layout.fillWidth: true }
                        Txt { text: [modelData.vendor, modelData.date].filter(s => s).join("  ·  "); muted: true; font.pixelSize: Style.fontSmall; Layout.fillWidth: true }
                    }
                }
            }
            Column {
                anchors.centerIn: parent
                width: parent.width - Style.px(32)
                visible: builds.count === 0
                spacing: Style.px(8)
                BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: JavaInstaller.loading.length > 0; visible: running }
                Txt {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    color: JavaInstaller.error ? Style.danger : Style.textMuted
                    text: JavaInstaller.error ? JavaInstaller.error
                        : JavaInstaller.loading ? qsTr("Loading the version list…")
                        : qsTr("Pick a Java version on the left.")
                }
            }
        }
    }

    function start() {
        if (build >= 0 && JavaInstaller.install(build)) {
            Router.toast(qsTr("Downloading Java. You can keep using the launcher."), false)
            close()
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: Style.px(8)
        QuietButton { text: qsTr("Cancel"); onClicked: dlg.close() }
        PrimaryButton {
            text: qsTr("Download")
            implicitHeight: Style.px(40)
            enabled: dlg.build >= 0
            onClicked: dlg.start()
        }
    }
}
