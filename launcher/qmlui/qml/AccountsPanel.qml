import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Side panel: pick the account to play with, add Microsoft or offline accounts.
Drawer {
    id: drawer
    edge: Qt.RightEdge
    width: Math.min(parent.width * 0.9, Style.px(400))
    height: parent.height
    modal: true
    property string mode: "list"     // list | microsoft | offline
    onClosed: { if (Accounts.signingIn) Accounts.cancelSignIn(); mode = "list"; offlineError.text = "" }

    background: Rectangle {
        color: Style.surface
        Rectangle { width: 1; height: parent.height; color: Style.line }
    }
    Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.5) }

    Connections {
        target: Accounts
        function onSignedIn() { drawer.mode = "list" }
    }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(24) }
        spacing: Style.px(16)

        RowLayout {
            Layout.fillWidth: true
            QuietButton {
                visible: drawer.mode !== "list"
                plain: true
                iconName: "arrow-left"
                tip: qsTr("Back")
                onClicked: { if (Accounts.signingIn) Accounts.cancelSignIn(); drawer.mode = "list" }
            }
            Text {
                Layout.fillWidth: true
                text: drawer.mode === "microsoft" ? qsTr("Microsoft account")
                    : drawer.mode === "offline" ? qsTr("Offline account") : qsTr("Accounts")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
            }
            QuietButton { plain: true; iconName: "close"; tip: qsTr("Close"); onClicked: drawer.close() }
        }

        // ---- list
        ColumnLayout {
            visible: drawer.mode === "list"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(10)

            Txt {
                visible: Accounts.count === 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: qsTr("Add an account to play. Sign in with Microsoft to play online, or add an offline account with any name.")
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: contentHeight
                clip: true
                spacing: Style.px(8)
                model: Accounts
                delegate: AbstractButton {
                    id: row
                    width: list.width
                    height: Style.px(60)
                    hoverEnabled: true
                    Accessible.name: model.name
                    onClicked: Accounts.setDefault(model.accountId)
                    background: Rectangle {
                        radius: Style.radius
                        color: row.hovered ? Style.surfaceHi : Style.alpha(Style.surfaceHi, 0.5)
                        border.width: model.isDefault ? 2 : 0
                        border.color: Style.accent
                    }
                    contentItem: RowLayout {
                        anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(6) }
                        spacing: Style.px(12)
                        Image {
                            source: model.face
                            Layout.preferredWidth: Style.px(36); Layout.preferredHeight: Style.px(36)
                            sourceSize: Qt.size(72, 72)
                            smooth: false
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Txt { text: model.name; font.weight: Font.Medium; Layout.fillWidth: true }
                            Txt {
                                Layout.fillWidth: true
                                font.pixelSize: Style.fontSmall
                                color: model.state === "error" ? Style.danger : Style.textMuted
                                text: model.state === "error" ? qsTr("Needs signing in again")
                                    : model.state === "working" ? qsTr("Checking…")
                                    : model.kind === "msa" ? qsTr("Microsoft") : qsTr("Offline")
                            }
                        }
                        QuietButton {
                            plain: true
                            visible: model.state === "error" && model.kind === "msa"
                            iconName: "refresh"; tip: qsTr("Sign in again")
                            onClicked: { drawer.mode = "microsoft"; Accounts.startMicrosoftSignIn() }
                        }
                        QuietButton {
                            plain: true
                            iconName: "trash"; tip: qsTr("Remove account")
                            onClicked: Accounts.remove(model.accountId)
                        }
                    }
                }
            }
            PrimaryButton {
                Layout.fillWidth: true
                text: qsTr("Sign in with Microsoft")
                onClicked: { drawer.mode = "microsoft"; Accounts.startMicrosoftSignIn() }
            }
            QuietButton {
                Layout.fillWidth: true
                iconName: "user"
                text: qsTr("Add offline account")
                onClicked: { drawer.mode = "offline"; offlineName.forceActiveFocus() }
            }
        }

        // ---- Microsoft sign-in
        ColumnLayout {
            visible: drawer.mode === "microsoft"
            Layout.fillWidth: true
            spacing: Style.px(14)

            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: Accounts.signInError.length > 0 ? Accounts.signInError
                    : Accounts.signingIn ? Accounts.signInStatus : qsTr("Signed in.")
                color: Accounts.signInError.length > 0 ? Style.danger : Style.textMuted
            }
            PrimaryButton {
                visible: Accounts.browserUrl.length > 0
                Layout.fillWidth: true
                iconName: "link"
                text: qsTr("Sign in in the browser")
                onClicked: Accounts.openBrowserSignIn()
            }
            // Device code: for when the browser on this computer is not an option
            Rectangle {
                visible: Accounts.deviceCode.length > 0
                Layout.fillWidth: true
                implicitHeight: codeCol.implicitHeight + Style.px(28)
                radius: Style.radius
                color: Style.alpha(Style.surfaceHi, 0.6)
                ColumnLayout {
                    id: codeCol
                    anchors { fill: parent; margins: Style.px(14) }
                    spacing: Style.px(10)
                    Txt {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        muted: true
                        text: qsTr("Or open %1 on any device and enter this code:").arg(Accounts.deviceUrl.replace("https://", ""))
                    }
                    Text {
                        text: Accounts.deviceCode
                        color: Style.text
                        font.family: Style.display
                        font.pixelSize: Style.px(30)
                        font.letterSpacing: 3
                    }
                    RowLayout {
                        spacing: Style.px(8)
                        QuietButton { iconName: "copy"; text: qsTr("Copy code"); onClicked: Accounts.copyDeviceCode() }
                        QuietButton { iconName: "link"; text: qsTr("Open page"); onClicked: Accounts.openDeviceSignIn() }
                    }
                }
            }
            QuietButton {
                visible: Accounts.signInError.length > 0
                text: qsTr("Try again")
                iconName: "refresh"
                onClicked: Accounts.startMicrosoftSignIn()
            }
        }

        // ---- offline
        ColumnLayout {
            visible: drawer.mode === "offline"
            Layout.fillWidth: true
            spacing: Style.px(12)
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: qsTr("Offline accounts play singleplayer and on servers with online mode turned off. The name is what other players see.")
            }
            Field {
                id: offlineName
                Layout.fillWidth: true
                placeholderText: qsTr("Player name")
                maximumLength: 16
                onAccepted: addOffline.clicked()
            }
            Txt { id: offlineError; visible: text.length > 0; color: Style.danger; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            PrimaryButton {
                id: addOffline
                Layout.fillWidth: true
                text: qsTr("Add account")
                onClicked: {
                    const err = Accounts.addOffline(offlineName.text)
                    offlineError.text = err
                    if (err.length === 0) { offlineName.text = ""; drawer.mode = "list" }
                }
            }
        }

        Item { Layout.fillHeight: true; visible: drawer.mode !== "list" }
    }
}
