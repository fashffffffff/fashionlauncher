import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Per-instance settings: account, Java, memory, window and play behaviour.
// Each group follows the launcher-wide settings until its "Override" switch is turned on.
Item {
    id: tab
    property string instanceId
    property var instance

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: Math.min(scroll.availableWidth, Style.px(780))
            spacing: Style.px(16)

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(12)
                Txt {
                    Layout.fillWidth: true
                    text: qsTr("Settings for this instance only. Changes are saved as you make them.")
                    muted: true
                    wrapMode: Text.WordWrap
                }
                QuietButton {
                    plain: true
                    iconName: "link"
                    text: qsTr("Advanced (classic)")
                    tip: qsTr("Opens the classic instance settings with every option, including ones not shown here.")
                    onClicked: Launcher.editInstance(tab.instanceId, "settings")
                }
                QuietButton {
                    plain: true
                    iconName: "gear"
                    text: qsTr("Launcher-wide settings")
                    onClicked: Router.go("settings")
                }
            }

            SettingsSection {
                id: acct
                instanceId: tab.instanceId
                title: qsTr("Account")
                description: qsTr("Play this instance with a specific account instead of your default one.")
                readonly property string chosen: val("UseAccountForInstance") === true ? (val("InstanceAccountId") || "") : ""
                readonly property var accountList: { Settings.revision; Accounts.count; return Settings.accounts() }
                SettingsRow {
                    label: qsTr("Play as")
                    SettingsDropdown {
                        Layout.preferredWidth: Style.px(280)
                        options: [{ value: "", label: qsTr("Default account") }].concat(
                                     acct.accountList.map(a => ({ value: a.profileId, label: a.name + "  ·  " + a.kind })))
                        value: acct.chosen
                        onPicked: (v) => {
                            if (v === "") {
                                Settings.instSet(tab.instanceId, "UseAccountForInstance", false)
                                Settings.instReset(tab.instanceId, "InstanceAccountId")
                            } else {
                                Settings.instSet(tab.instanceId, "InstanceAccountId", v)
                                Settings.instSet(tab.instanceId, "UseAccountForInstance", true)
                            }
                        }
                    }
                }
            }

            SettingsMemory { instanceId: tab.instanceId }
            SettingsJava { instanceId: tab.instanceId }
            SettingsWindow { instanceId: tab.instanceId }
            SettingsBehaviour { instanceId: tab.instanceId }
            Item { implicitHeight: Style.px(8) }
        }
    }
}
