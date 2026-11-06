import QtQuick
import QtQuick.Layouts

// One friend: name, what they're doing, how we're connected.
Rectangle {
    id: row
    property var friend: ({})
    signal invite()
    implicitHeight: Style.px(64)
    radius: Style.radius
    color: hover.hovered ? Style.alpha(Style.text, 0.05) : "transparent"

    readonly property string statusText: {
        const f = row.friend
        if (f.pending) return qsTr("Waiting for them to accept your request")
        if (!f.online) return qsTr("Offline")
        if (f.status === "hosting") return f.game ? qsTr("Hosting %1").arg(f.game) : qsTr("Hosting a world")
        if (f.status === "playing") return f.game ? qsTr("Playing %1").arg(f.game) : qsTr("Playing")
        return qsTr("Online")
    }
    readonly property string connText: {
        const f = row.friend
        if (!f.online && !f.pending) return ""
        if (f.conn === "direct") return qsTr("Direct connection")
        if (f.conn === "connecting") return qsTr("Connecting…")
        if (f.conn === "failed") return qsTr("Couldn't reach — ask your friend to host")
        return ""
    }

    HoverHandler { id: hover }
    RowLayout {
        anchors { fill: parent; leftMargin: Style.px(10); rightMargin: Style.px(8) }
        spacing: Style.px(12)
        FriendsAvatar { name: row.friend.name || ""; online: !!row.friend.online && !row.friend.pending }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.px(2)
            Txt { text: row.friend.name || ""; font.weight: Font.DemiBold; Layout.fillWidth: true }
            RowLayout {
                spacing: Style.px(8)
                Layout.fillWidth: true
                Txt { text: row.statusText; muted: true; font.pixelSize: Style.fontSmall; Layout.fillWidth: true }
                Rectangle {
                    visible: row.connText.length > 0
                    implicitWidth: connLabel.implicitWidth + Style.px(12); implicitHeight: connLabel.implicitHeight + Style.px(4)
                    radius: height / 2
                    color: Style.alpha(row.friend.conn === "direct" ? Style.success : row.friend.conn === "failed" ? Style.danger : Style.text, 0.14)
                    Layout.maximumWidth: Style.px(300)
                    Txt {
                        id: connLabel
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width - Style.px(12))
                        text: row.connText
                        font.pixelSize: Style.fontSmall
                        color: row.friend.conn === "direct" ? Style.success : row.friend.conn === "failed" ? Style.danger : Style.textMuted
                    }
                }
            }
        }
        QuietButton {
            visible: row.friend.conn === "failed"
            iconName: "refresh"; tip: qsTr("Try again")
            onClicked: Friends.retry()
        }
        QuietButton {
            visible: !row.friend.pending && row.friend.conn === "direct"
            text: qsTr("Invite"); iconName: "play"
            onClicked: row.invite()
        }
        QuietButton {
            iconName: "more"; plain: true; tip: qsTr("More")
            onClicked: menu.popup()
        }
    }
    ThemedMenu {
        id: menu
        ThemedMenuItem { text: qsTr("Copy friend code"); iconName: "copy"; onTriggered: { Friends.copyText(row.friend.code); Router.toast(qsTr("Code copied"), false) } }
        ThemedMenuItem {
            text: row.friend.pending ? qsTr("Cancel request") : qsTr("Remove friend")
            iconName: "trash"; danger: true
            onTriggered: Friends.removeFriend(row.friend.key)
        }
    }
}
