import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings → AI assistants: let Claude, Codex and other AI agents control the launcher through its MCP server
// (AssistantsBridge, ControlServer). Everything stays on this computer; account tokens are never shared.
ColumnLayout {
    id: ai
    spacing: Style.px(16)
    property string note: ""
    property bool noteError: false

    Connections {
        target: Assistants
        function onMessage(text, isError) { ai.note = text; ai.noteError = isError }
    }
    Component.onCompleted: Assistants.refresh()

    SettingsSection {
        title: qsTr("AI assistants")
        description: qsTr("Let an AI assistant manage the launcher for you: create instances, find and install mods, start the game, read logs and explain crashes. It works on this computer only. Account passwords and tokens are never shared, and deleting an instance always asks you here first.")

        Repeater {
            model: Assistants.clients
            delegate: SettingsRow {
                required property var modelData
                label: modelData.name
                description: modelData.connected ? qsTr("Connected")
                           : modelData.detected ? qsTr("Found on this computer")
                           : qsTr("Not found on this computer")
                QuietButton {
                    visible: modelData.connected
                    text: qsTr("Disconnect")
                    enabled: !Assistants.busy
                    onClicked: Assistants.disconnectClient(modelData.id)
                }
                PrimaryButton {
                    visible: !modelData.connected
                    implicitHeight: Style.px(40)
                    text: qsTr("Connect")
                    enabled: modelData.detected && !Assistants.busy
                    onClicked: Assistants.connectClient(modelData.id)
                }
            }
        }

        SettingsNote {
            text: ai.note
            kind: ai.noteError ? "danger" : "info"
        }
    }

    SettingsSection {
        title: qsTr("Other AI apps")
        description: qsTr("Any app that supports MCP servers (Cursor, Windsurf, LM Studio and others): add this to its MCP settings.")

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: jsonText.implicitHeight + Style.px(20)
            radius: Style.radius
            color: Style.alpha(Style.bg, 0.55)
            border.width: 1
            border.color: Style.line
            TextEdit {
                id: jsonText
                anchors { fill: parent; margins: Style.px(10) }
                text: Assistants.mcpJson
                readOnly: true
                selectByMouse: true
                wrapMode: Text.WrapAnywhere
                color: Style.text
                selectionColor: Style.alpha(Style.accent, 0.4)
                font.family: Qt.platform.os === "windows" ? "Consolas" : Qt.platform.os === "osx" ? "Menlo" : "monospace"
                font.pixelSize: Style.fontSmall
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            QuietButton {
                iconName: "copy"
                text: qsTr("Copy settings")
                onClicked: { Assistants.copy(Assistants.mcpJson); Router.toast(qsTr("Copied."), false) }
            }
            QuietButton {
                iconName: "terminal"
                text: qsTr("Copy terminal command")
                tip: qsTr("For scripts and agents without MCP: the same commands, answers in JSON")
                onClicked: { Assistants.copy(Assistants.ctlCommand); Router.toast(qsTr("Copied."), false) }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
