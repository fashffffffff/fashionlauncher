import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// First run: language, account, first instance. Each step can be skipped.
Item {
    id: ob
    visible: !Launcher.onboardingDone
    property int step: 0
    readonly property int steps: Instances.count === 0 ? 3 : 2
    function next() { if (step + 1 >= steps) Launcher.finishOnboarding(); else step++ }

    // The languages the interface is fully translated into, already in display order.
    readonly property var languages: Launcher.languages

    Rectangle { anchors.fill: parent; color: Style.alpha(Style.scrim, 0.78) }
    MouseArea { anchors.fill: parent }   // block the app underneath

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Style.px(80), Style.px(560))
        height: Math.min(parent.height - Style.px(80), Style.px(560))
        radius: Style.radius + Style.px(6)
        color: Style.surface
        border.width: 1
        border.color: Style.line

        ColumnLayout {
            anchors { fill: parent; margins: Style.px(32) }
            spacing: Style.px(16)

            // progress
            Row {
                spacing: Style.px(6)
                Repeater {
                    model: ob.steps
                    Rectangle {
                        width: Style.px(28); height: Style.px(4); radius: 2
                        color: index <= ob.step ? Style.accent : Style.line
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: [qsTr("Choose your language"), qsTr("Add an account"), qsTr("Create your first instance")][ob.step]
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.fontTitle + Style.px(4)
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: [qsTr("You can change it later in Settings."),
                       qsTr("Sign in with Microsoft to play online, or create an offline account with any name."),
                       qsTr("An instance is a separate Minecraft installation with its own version, mods and worlds.")][ob.step]
            }

            // Step 1: language
            Field {
                id: langSearch
                visible: ob.step === 0
                Layout.fillWidth: true
                iconName: "search"
                placeholderText: qsTr("Search languages")
            }
            ListView {
                visible: ob.step === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Style.px(4)
                model: {
                    const q = langSearch.text.trim().toLowerCase()
                    return q ? ob.languages.filter(l => l.name.toLowerCase().indexOf(q) >= 0 || l.key.toLowerCase().indexOf(q) >= 0)
                             : ob.languages
                }
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: AbstractButton {
                    id: langRow
                    width: ListView.view.width - Style.px(10)
                    height: Style.px(40)
                    hoverEnabled: true
                    readonly property bool on: modelData.key === Launcher.language
                    Accessible.name: modelData.name
                    onClicked: Launcher.setLanguage(modelData.key)
                    background: Rectangle {
                        radius: Style.radius
                        color: langRow.on ? Style.alpha(Style.accent, 0.18) : langRow.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                        border.width: langRow.on ? 1 : 0
                        border.color: Style.accent
                    }
                    contentItem: RowLayout {
                        anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                        Txt { text: modelData.name; Layout.fillWidth: true; font.weight: langRow.on ? Font.DemiBold : Font.Normal }
                        Icon { visible: langRow.on; name: "check"; color: Style.accent; size: Style.px(16) }
                    }
                }
            }

            // Step 2: account
            ColumnLayout {
                visible: ob.step === 1
                Layout.fillWidth: true
                spacing: Style.px(10)
                Rectangle {
                    visible: !!Accounts.current.name
                    Layout.fillWidth: true
                    implicitHeight: Style.px(54)
                    radius: Style.radius
                    color: Style.alpha(Style.success, 0.14)
                    RowLayout {
                        anchors { fill: parent; margins: Style.px(12) }
                        spacing: Style.px(10)
                        Image { source: Accounts.current.face || ""; Layout.preferredWidth: Style.px(30); Layout.preferredHeight: Style.px(30); smooth: false }
                        Txt { text: qsTr("Signed in as %1").arg(Accounts.current.name || ""); Layout.fillWidth: true }
                        Icon { name: "check"; color: Style.success }
                    }
                }
                PrimaryButton { Layout.fillWidth: true; text: qsTr("Sign in with Microsoft"); onClicked: Router.openAccounts() }
                QuietButton { Layout.fillWidth: true; iconName: "user"; text: qsTr("Add offline account"); onClicked: Router.openAccounts() }
            }

            // Step 3: first instance
            ColumnLayout {
                visible: ob.step === 2
                Layout.fillWidth: true
                PrimaryButton {
                    Layout.fillWidth: true
                    iconName: "plus"
                    text: qsTr("Create instance")
                    onClicked: { Launcher.finishOnboarding(); Router.open("create") }
                }
            }

            Item { Layout.fillHeight: true; visible: ob.step !== 0 }

            RowLayout {
                Layout.fillWidth: true
                QuietButton { visible: ob.step > 0; plain: true; text: qsTr("Back"); iconName: "arrow-left"; onClicked: ob.step-- }
                Item { Layout.fillWidth: true }
                QuietButton {
                    plain: true
                    visible: ob.step > 0
                    text: qsTr("Skip")
                    onClicked: ob.next()
                }
                PrimaryButton {
                    implicitHeight: Style.px(42)
                    text: ob.step + 1 >= ob.steps ? qsTr("Done") : qsTr("Continue")
                    onClicked: ob.next()
                }
            }
        }
    }
}
