import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Launcher settings: sections on the left, their settings on the right. Every change applies at once.
Item {
    id: page
    property var params: ({})
    property string section: params.section || "launcher"

    readonly property var sections: [
        { id: "launcher", icon: "gear", label: qsTr("Launcher"), classic: "launcher-settings" },
        { id: "java", icon: "cube", label: qsTr("Java and memory"), classic: "java-settings" },
        { id: "game", icon: "play", label: qsTr("Game"), classic: "minecraft-settings" },
        { id: "network", icon: "globe", label: qsTr("Network"), classic: "proxy-settings" }
    ]
    readonly property var current: sections.find(s => s.id === section) || sections[0]

    function val(key) { Settings.revision; return Settings.get(key) }

    RowLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(32)

        // Section list
        ColumnLayout {
            Layout.preferredWidth: Style.px(240)
            Layout.fillHeight: true
            spacing: Style.px(6)
            Text {
                text: qsTr("Settings")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(34)
                font.weight: Font.DemiBold
                Layout.bottomMargin: Style.px(10)
            }
            Repeater {
                model: page.sections
                delegate: AbstractButton {
                    id: nav
                    readonly property bool active: page.current.id === modelData.id
                    Layout.fillWidth: true
                    implicitHeight: Style.px(44)
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: modelData.label
                    onClicked: page.section = modelData.id
                    background: Rectangle {
                        radius: Style.radius
                        color: nav.active ? Style.alpha(Style.accent, 0.18) : nav.hovered ? Style.alpha(Style.text, 0.07) : "transparent"
                        border.width: nav.visualFocus ? 2 : 0
                        border.color: Style.accent
                    }
                    contentItem: RowLayout {
                        spacing: Style.px(12)
                        Icon {
                            Layout.leftMargin: Style.px(12)
                            name: modelData.icon
                            size: Style.px(18)
                            color: nav.active ? Style.accent : Style.text
                        }
                        Txt {
                            Layout.fillWidth: true
                            text: modelData.label
                            font.weight: nav.active ? Font.DemiBold : Font.Normal
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            Txt {
                Layout.fillWidth: true
                text: qsTr("Changes are saved as you make them.")
                muted: true
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSmall
            }
            QuietButton {
                Layout.fillWidth: true
                plain: true
                iconName: "link"
                text: qsTr("Advanced (classic)")
                tip: qsTr("Opens the classic settings window with every option, including ones not shown here.")
                onClicked: Launcher.openSettings(page.current.classic)
            }
        }

        // Settings of the chosen section
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            Loader {
                width: Math.min(scroll.availableWidth, Style.px(760))
                sourceComponent: ({ launcher: launcherPane, java: javaPane, game: gamePane, network: networkPane })[page.current.id]
            }
        }
    }

    // ---- Launcher ----
    Component {
        id: launcherPane
        ColumnLayout {
            spacing: Style.px(16)

            SettingsSection {
                title: qsTr("Language")
                description: qsTr("Changes the language right away.")
                SettingsRow {
                    label: qsTr("Interface language")
                    SettingsDropdown {
                        Layout.preferredWidth: Style.px(280)
                        options: Launcher.languages.map(l => ({ value: l.key, label: l.name }))
                        value: Launcher.language
                        onPicked: (v) => Launcher.setLanguage(v)
                    }
                }
            }

            SettingsBehaviour {}

            SettingsSection {
                id: folders
                title: qsTr("Instances folder")
                description: qsTr("Where your instances (game installs) are stored. Instances already in the old folder stay there.")
                readonly property string dir: page.val("InstanceDir") || ""
                property string problem: Settings.instanceDirProblem(dir)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(8)
                    Field {
                        id: dirField
                        Layout.fillWidth: true
                        text: folders.dir
                        font.family: "Menlo"
                        font.pixelSize: Style.fontSmall
                        Accessible.name: qsTr("Instances folder")
                        onEditingFinished: if (text.length && text !== folders.dir) Settings.set("InstanceDir", text)
                    }
                    QuietButton { iconName: "folder"; text: qsTr("Browse…"); onClicked: dirDialog.open() }
                    QuietButton { iconName: "export"; tip: qsTr("Open folder"); onClicked: Settings.openFolder(folders.dir) }
                }
                SettingsNote { text: folders.problem; kind: "warn" }
                FolderDialog {
                    id: dirDialog
                    title: qsTr("Choose the instances folder")
                    onAccepted: {
                        const p = Settings.folderFromUrl(selectedFolder)
                        if (p.length) Settings.set("InstanceDir", p)
                    }
                }
            }

            SettingsSection {
                title: qsTr("Interface")
                SettingsSwitch {
                    label: qsTr("Use the classic interface")
                    description: qsTr("Switch back to the old Prism Launcher window. Takes effect after you restart %1.").arg(Launcher.appName)
                    checked: page.val("UseClassicUI") === true
                    onToggled: (on) => {
                        Settings.set("UseClassicUI", on)
                        Router.toast(on ? qsTr("Restart %1 to use the classic interface.").arg(Launcher.appName)
                                        : qsTr("The new interface stays on."), false)
                    }
                }
                SettingsSwitch {
                    label: qsTr("Check for game updates when starting")
                    description: qsTr("Refresh version information each time you press Play.")
                    checked: page.val("MetaRefreshOnLaunch") === true
                    onToggled: (on) => Settings.set("MetaRefreshOnLaunch", on)
                }
            }
            Item { implicitHeight: Style.px(16) }
        }
    }

    // ---- Java and memory ----
    Component {
        id: javaPane
        ColumnLayout {
            spacing: Style.px(16)
            SettingsMemory {}
            SettingsJava {}
            Item { implicitHeight: Style.px(16) }
        }
    }

    // ---- Game ----
    Component {
        id: gamePane
        ColumnLayout {
            spacing: Style.px(16)
            SettingsWindow {}
            SettingsSection {
                id: logSec
                title: qsTr("Game log")
                description: qsTr("The log window shows what the game prints while it runs. It helps find out why a game crashed.")
                SettingsField {
                    label: qsTr("Lines to keep")
                    description: qsTr("Older lines are dropped once the log is this long.")
                    fieldWidth: Style.px(120)
                    value: String(logSec.val("ConsoleMaxLines"))
                    validator: IntValidator { bottom: 10000; top: 1000000 }
                    onCommitted: (v) => logSec.put("ConsoleMaxLines", parseInt(v))
                }
                SettingsSwitch {
                    label: qsTr("Stop logging at the limit")
                    description: qsTr("Pause the log instead of dropping old lines. Useful when a game floods it.")
                    checked: logSec.val("ConsoleOverflowStop") === true
                    onToggled: (on) => logSec.put("ConsoleOverflowStop", on)
                }
                SettingsSwitch {
                    label: qsTr("Include output of custom commands")
                    description: qsTr("Also log what pre-launch and post-exit commands print.")
                    checked: logSec.val("LogPrePostOutput") === true
                    onToggled: (on) => logSec.put("LogPrePostOutput", on)
                }
            }
            Item { implicitHeight: Style.px(16) }
        }
    }

    // ---- Network ----
    Component {
        id: networkPane
        ColumnLayout {
            spacing: Style.px(16)
            SettingsSection {
                id: proxy
                title: qsTr("Proxy")
                description: qsTr("Only needed if your network requires one. Applies to downloads and sign-in right away.")
                readonly property string type: proxy.val("ProxyType") || "None"
                readonly property bool custom: type === "SOCKS5" || type === "HTTP"
                SettingsRow {
                    label: qsTr("Connection")
                    SettingsSegmented {
                        options: [{ value: "Default", label: qsTr("System settings") }, { value: "None", label: qsTr("No proxy") },
                                  { value: "SOCKS5", label: "SOCKS5" }, { value: "HTTP", label: "HTTP" }]
                        value: proxy.type
                        onPicked: (v) => proxy.put("ProxyType", v)
                    }
                }
                ColumnLayout {
                    visible: proxy.custom
                    Layout.fillWidth: true
                    spacing: Style.px(16)
                    SettingsField {
                        label: qsTr("Address")
                        description: qsTr("Host name or IP address of the proxy.")
                        value: proxy.val("ProxyAddr") || ""
                        placeholder: "127.0.0.1"
                        onCommitted: (v) => proxy.put("ProxyAddr", v)
                    }
                    SettingsField {
                        label: qsTr("Port")
                        fieldWidth: Style.px(120)
                        value: String(proxy.val("ProxyPort"))
                        validator: IntValidator { bottom: 0; top: 65535 }
                        onCommitted: (v) => proxy.put("ProxyPort", parseInt(v))
                    }
                    SettingsField {
                        label: qsTr("User name")
                        description: qsTr("Leave empty if the proxy doesn't ask for one.")
                        value: proxy.val("ProxyUser") || ""
                        onCommitted: (v) => proxy.put("ProxyUser", v)
                    }
                    SettingsRow {
                        label: qsTr("Password")
                        Field {
                            Layout.preferredWidth: Style.px(240)
                            echoMode: TextInput.Password
                            text: proxy.val("ProxyPass") || ""
                            Accessible.name: qsTr("Proxy password")
                            onEditingFinished: proxy.put("ProxyPass", text)
                        }
                    }
                    SettingsNote { text: qsTr("The password is stored unencrypted in the launcher's settings file."); kind: "info" }
                }
            }
            Item { implicitHeight: Style.px(16) }
        }
    }
}
