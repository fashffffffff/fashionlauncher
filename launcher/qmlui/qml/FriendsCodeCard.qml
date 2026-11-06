import QtQuick
import QtQuick.Layouts

// Your name and friend code, and adding a friend by code or link.
Rectangle {
    id: card
    implicitHeight: col.implicitHeight + Style.px(40)
    radius: Style.radius
    color: Style.panel(0)
    border.width: 1
    border.color: Style.panelBorder
    clip: true

    function addFromField() {
        const t = addField.text.trim()
        if (!t.length) return
        if (!Friends.looksLikeCode(t)) { Router.toast(qsTr("That doesn't look like a friend code."), true); return }
        Friends.addFriend(t)
        addField.text = ""
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(20) }
        spacing: Style.px(12)

        Txt { text: qsTr("Your friend code"); font.family: Style.display; font.pixelSize: Style.fontLead }
        Txt {
            text: qsTr("Send it to a friend. When they add it, you'll get a request here.")
            muted: true; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: codeText.implicitHeight + Style.px(20)
            radius: Style.radius
            color: Style.alpha(Style.bg, 0.4)
            border.width: 1
            border.color: Style.line
            Text {
                id: codeText
                anchors { fill: parent; margins: Style.px(10) }
                text: Friends.me.code || (Friends.available ? qsTr("Getting your code…") : "")
                color: Style.text
                font.family: "Menlo"
                font.pixelSize: Style.fontBody
                wrapMode: Text.WrapAnywhere
                verticalAlignment: Text.AlignVCenter
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Style.px(8)
            QuietButton {
                text: qsTr("Copy"); iconName: "copy"
                enabled: !!Friends.me.code
                onClicked: { Friends.copyText(Friends.me.code); Router.toast(qsTr("Code copied"), false) }
            }
            QuietButton {
                text: qsTr("Copy invite link"); iconName: "link"
                enabled: !!Friends.me.link
                onClicked: { Friends.copyText(Friends.me.link); Router.toast(qsTr("Invite link copied"), false) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Txt { text: qsTr("Name friends see"); muted: true }
            Field {
                id: nameField
                Layout.fillWidth: true
                Layout.minimumWidth: Style.px(80)
                text: Friends.displayName
                placeholderText: qsTr("Your name")
                maximumLength: 32
                onEditingFinished: if (text.trim() !== Friends.displayName) Friends.setDisplayName(text)
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Style.line }

        Txt { text: qsTr("Add a friend"); font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Field {
                id: addField
                Layout.fillWidth: true
                Layout.minimumWidth: Style.px(80)
                iconName: "user"
                placeholderText: qsTr("Paste a friend code or invite link")
                onAccepted: card.addFromField()
            }
            PrimaryButton {
                text: qsTr("Add")
                implicitWidth: Style.px(90); implicitHeight: Style.px(40)
                Layout.preferredWidth: Style.px(90)
                enabled: addField.text.trim().length > 0 && Friends.available
                onClicked: card.addFromField()
            }
        }

        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSmall
            muted: true
            text: {
                if (!Friends.available) return Friends.error === "missing"
                    ? qsTr("The networking helper is missing from this install, so Friends can't work.")
                    : qsTr("Starting…")
                if (!Friends.net.dht) return qsTr("Looking for the network…")
                const nat = Friends.net.nat
                if (nat === "hard") return qsTr("Online. Your network is strict: some friends may not be able to connect directly.")
                if (nat === "unknown") return qsTr("Online. Couldn't check your network yet.")
                return qsTr("Online. Your network allows direct connections.")
            }
        }
    }
}
