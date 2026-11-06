import QtQuick
import QtQuick.Layouts

// Size of the game window. Global or per instance.
SettingsSection {
    id: ws
    group: "window"
    title: qsTr("Game window")
    description: qsTr("The size of the Minecraft window when it opens.")
    readonly property bool maximized: val("LaunchMaximized") === true

    SettingsSwitch {
        label: qsTr("Start maximized")
        description: qsTr("Open the game filling the whole screen area.")
        checked: ws.maximized
        onToggled: (on) => ws.put("LaunchMaximized", on)
    }
    SettingsRow {
        label: qsTr("Window size")
        description: qsTr("Width × height in pixels.")
        enabled: !ws.maximized
        opacity: enabled ? 1 : 0.5
        Field {
            Layout.preferredWidth: Style.px(90)
            horizontalAlignment: TextInput.AlignRight
            text: ws.val("MinecraftWinWidth")
            validator: IntValidator { bottom: 1; top: 65536 }
            Accessible.name: qsTr("Window width")
            onEditingFinished: if (acceptableInput) ws.put("MinecraftWinWidth", parseInt(text))
        }
        Txt { text: "×"; muted: true }
        Field {
            Layout.preferredWidth: Style.px(90)
            horizontalAlignment: TextInput.AlignRight
            text: ws.val("MinecraftWinHeight")
            validator: IntValidator { bottom: 1; top: 65536 }
            Accessible.name: qsTr("Window height")
            onEditingFinished: if (acceptableInput) ws.put("MinecraftWinHeight", parseInt(text))
        }
        QuietButton {
            plain: true
            text: qsTr("Reset")
            tip: qsTr("Back to 854 × 480")
            onClicked: { ws.put("MinecraftWinWidth", Settings.defaultValue("MinecraftWinWidth")); ws.put("MinecraftWinHeight", Settings.defaultValue("MinecraftWinHeight")) }
        }
    }
}
