import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Which Java runs the game, and with which extra arguments. Global or per instance.
SettingsSection {
    id: js
    group: "java"
    title: qsTr("Java")
    description: qsTr("Minecraft runs on Java. Newer game versions need newer Java.")
    readonly property bool global: instanceId.length === 0
    readonly property string javaPath: val("JavaPath") || ""
    property var testResult: null     // { ok, title, details }

    Component.onCompleted: if (Settings.javaInstalls.length === 0) Settings.scanJava()
    Connections {
        target: Settings
        function onJavaTestFinished(ok, title, details) { js.testResult = { ok: ok, title: title, details: details } }
    }

    // global-only automatic Java
    SettingsSwitch {
        visible: js.global
        label: qsTr("Pick the right Java automatically")
        description: qsTr("Each instance uses an installed Java that matches its Minecraft version.")
        checked: js.val("AutomaticJavaSwitch") === true
        onToggled: (on) => { js.put("AutomaticJavaSwitch", on); if (!on) js.put("AutomaticJavaDownload", false) }
    }
    SettingsSwitch {
        visible: js.global && Settings.javaDownloaderEnabled
        enabled: js.val("AutomaticJavaSwitch") === true
        opacity: enabled ? 1 : 0.5
        label: qsTr("Download Java automatically when it's missing")
        checked: js.val("AutomaticJavaDownload") === true
        onToggled: (on) => js.put("AutomaticJavaDownload", on)
    }

    // the Java path
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.px(8)
        Txt { text: js.global ? qsTr("Default Java") : qsTr("Java for this instance") }
        Txt {
            Layout.fillWidth: true
            visible: js.global
            text: qsTr("Used by instances that don't choose their own.")
            muted: true; wrapMode: Text.WordWrap; font.pixelSize: Style.fontSmall
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Field {
                id: pathField
                Layout.fillWidth: true
                text: js.javaPath
                placeholderText: qsTr("Path to the java executable")
                font.family: "Menlo"
                font.pixelSize: Style.fontSmall
                Accessible.name: qsTr("Java path")
                onEditingFinished: if (text !== js.javaPath) { js.put("JavaPath", text); js.testResult = null }
            }
            QuietButton { iconName: "folder"; text: qsTr("Browse…"); onClicked: javaDialog.open() }
            QuietButton {
                iconName: "check"
                text: Settings.javaTesting ? qsTr("Testing…") : qsTr("Test")
                enabled: !Settings.javaTesting && js.javaPath.length > 0
                onClicked: {
                    js.testResult = null
                    Settings.testJava(js.javaPath, js.val("JvmArgs") || "", js.val("MinMemAlloc"), js.val("MaxMemAlloc"), js.val("PermGen"))
                }
            }
        }
        SettingsNote {
            text: js.testResult ? js.testResult.title + (js.testResult.details ? " — " + js.testResult.details : "") : ""
            kind: js.testResult && !js.testResult.ok ? "danger" : "info"
        }
    }

    // detected installs
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.px(8)
        RowLayout {
            Layout.fillWidth: true
            Txt { text: qsTr("Java found on this computer"); Layout.fillWidth: true }
            QuietButton {
                plain: true
                iconName: "refresh"
                text: Settings.javaScanning ? qsTr("Searching…") : qsTr("Search again")
                enabled: !Settings.javaScanning
                onClicked: Settings.scanJava()
            }
            QuietButton {
                visible: Settings.javaDownloaderEnabled
                plain: true
                iconName: "export"
                text: qsTr("Download Java…")
                onClicked: Settings.downloadJava(js.instanceId)
            }
        }
        Txt {
            visible: !Settings.javaScanning && Settings.javaInstalls.length === 0
            text: qsTr("No Java installations were found. Download one or choose the java executable yourself.")
            muted: true; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        Rectangle {
            visible: Settings.javaInstalls.length > 0
            Layout.fillWidth: true
            implicitHeight: list.implicitHeight + 2
            radius: Style.radius
            color: "transparent"
            border.width: 1
            border.color: Style.line
            clip: true
            Column {
                id: list
                x: 1; y: 1
                width: parent.width - 2
                Repeater {
                    model: Settings.javaInstalls
                    delegate: AbstractButton {
                        id: jrow
                        readonly property bool active: modelData.path === js.javaPath
                        width: list.width
                        height: Style.px(44)
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: qsTr("Use Java %1").arg(modelData.version)
                        onClicked: { js.put("JavaPath", modelData.path); js.testResult = null }
                        background: Rectangle {
                            radius: Style.radius - 2
                            color: jrow.active ? Style.alpha(Style.accent, 0.16) : jrow.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                            border.width: jrow.visualFocus ? 2 : 0
                            border.color: Style.accent
                        }
                        contentItem: RowLayout {
                            spacing: Style.px(12)
                            Txt {
                                Layout.leftMargin: Style.px(12)
                                Layout.preferredWidth: Style.px(110)
                                text: qsTr("Java %1").arg(modelData.major)
                                font.weight: Font.DemiBold
                            }
                            Txt {
                                Layout.preferredWidth: Style.px(150)
                                text: modelData.version + (modelData.arch ? "  ·  " + modelData.arch : "")
                                muted: true
                                font.pixelSize: Style.fontSmall
                            }
                            Txt {
                                Layout.fillWidth: true
                                text: modelData.path
                                muted: true
                                elide: Text.ElideMiddle
                                font.pixelSize: Style.fontSmall
                            }
                            Icon {
                                visible: jrow.active
                                name: "check"; color: Style.accent; size: Style.px(16)
                                Layout.rightMargin: Style.px(12)
                            }
                        }
                    }
                }
            }
        }
    }

    SettingsSwitch {
        label: qsTr("Skip the Java compatibility check")
        description: qsTr("Launch even if this Java looks wrong for the Minecraft version. Only for troubleshooting.")
        checked: js.val("IgnoreJavaCompatibility") === true
        onToggled: (on) => js.put("IgnoreJavaCompatibility", on)
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.px(6)
        Txt { text: qsTr("Extra Java arguments") }
        Txt {
            Layout.fillWidth: true
            text: qsTr("Advanced. Passed to Java when the game starts, separated by spaces.")
            muted: true; wrapMode: Text.WordWrap; font.pixelSize: Style.fontSmall
        }
        Field {
            id: argsField
            Layout.fillWidth: true
            text: js.val("JvmArgs") || ""
            placeholderText: qsTr("For example: -XX:+UseG1GC")
            font.family: "Menlo"
            font.pixelSize: Style.fontSmall
            Accessible.name: qsTr("Extra Java arguments")
            onEditingFinished: if (text !== (js.val("JvmArgs") || "")) js.put("JvmArgs", text)
        }
        SettingsNote { text: Settings.jvmArgsProblem(argsField.text); kind: "warn" }
    }

    FileDialog {
        id: javaDialog
        title: qsTr("Choose the java executable")
        onAccepted: {
            const p = Settings.javaPathFromUrl(selectedFile)
            if (p.length) { js.put("JavaPath", p); js.testResult = null }
            else Router.toast(qsTr("That file isn't a program Java can run from."), true)
        }
    }
}
