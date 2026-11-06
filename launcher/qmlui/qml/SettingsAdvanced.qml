import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Options for people who tinker: custom commands, environment variables, graphics and old-version fixes.
// Global or per instance (each card has its own "Override" switch on an instance).
ColumnLayout {
    id: adv
    property string instanceId: ""
    spacing: Style.px(16)

    SettingsSection {
        id: cmds
        instanceId: adv.instanceId
        group: "commands"
        title: qsTr("Custom commands")
        description: qsTr("Run your own commands around the game. $INST_DIR, $INST_MC_DIR, $INST_JAVA and similar variables are filled in.")
        SettingsField {
            label: qsTr("Before launch")
            fieldWidth: Style.px(320)
            placeholder: qsTr("e.g. a backup script")
            value: cmds.val("PreLaunchCommand") || ""
            onCommitted: (v) => cmds.put("PreLaunchCommand", v)
        }
        SettingsField {
            label: qsTr("Wrapper")
            description: qsTr("Starts Java through this program, like “prime-run”.")
            fieldWidth: Style.px(320)
            value: cmds.val("WrapperCommand") || ""
            onCommitted: (v) => cmds.put("WrapperCommand", v)
        }
        SettingsField {
            label: qsTr("After exit")
            fieldWidth: Style.px(320)
            value: cmds.val("PostExitCommand") || ""
            onCommitted: (v) => cmds.put("PostExitCommand", v)
        }
    }

    SettingsSection {
        id: env
        instanceId: adv.instanceId
        group: "env"
        title: qsTr("Environment variables")
        description: qsTr("Extra variables for the game process, one NAME=value per line.")
        function toText(json) {
            try {
                const o = JSON.parse(json || "{}")
                return Object.keys(o).map(k => k + "=" + o[k]).join("\n")
            } catch (e) { return "" }
        }
        function fromText(text) {
            const o = {}
            for (const line of text.split("\n")) {
                const i = line.indexOf("=")
                if (i > 0) o[line.slice(0, i).trim()] = line.slice(i + 1)
            }
            return JSON.stringify(o)
        }
        TextArea {
            id: envText
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(Style.px(90), implicitHeight)
            text: env.toText(env.val("Env"))
            placeholderText: "MESA_GL_VERSION_OVERRIDE=4.5"
            placeholderTextColor: Style.textMuted
            color: Style.text
            font.family: Qt.platform.os === "windows" ? "Consolas" : Qt.platform.os === "osx" ? "Menlo" : "monospace"
            font.pixelSize: Style.fontSmall
            wrapMode: TextEdit.NoWrap
            selectByMouse: true
            Accessible.name: qsTr("Environment variables")
            onActiveFocusChanged: if (!activeFocus) {
                const json = env.fromText(text)
                if (json !== JSON.stringify(JSON.parse(env.val("Env") || "{}"))) env.put("Env", json)
            }
            background: Rectangle {
                radius: Style.radius
                color: Style.alpha(Style.surface, 0.85)
                border.width: envText.activeFocus ? 2 : 1
                border.color: envText.activeFocus ? Style.accent : Style.line
            }
        }
    }

    SettingsSection {
        id: perf
        instanceId: adv.instanceId
        group: "performance"
        title: qsTr("Graphics")
        description: qsTr("Which graphics card and drivers the game uses.")
        SettingsSwitch {
            label: qsTr("Use the discrete graphics card")
            description: qsTr("On laptops with two graphics cards, run the game on the faster one.")
            checked: perf.val("UseDiscreteGpu") === true
            onToggled: (on) => perf.put("UseDiscreteGpu", on)
        }
        SettingsSwitch {
            label: qsTr("Use Zink (OpenGL over Vulkan)")
            description: qsTr("Can help where the OpenGL driver is broken. Usually slower.")
            checked: perf.val("UseZink") === true
            onToggled: (on) => perf.put("UseZink", on)
        }
    }

    SettingsSection {
        id: legacy
        instanceId: adv.instanceId
        group: "legacy"
        title: qsTr("Old versions")
        description: qsTr("Fixes for Minecraft versions from before 1.6.")
        SettingsSwitch {
            label: qsTr("Fix skins, sounds and logins")
            description: qsTr("Redirects services the old game expects but that no longer exist.")
            checked: legacy.val("OnlineFixes") === true
            onToggled: (on) => legacy.put("OnlineFixes", on)
        }
    }
}
