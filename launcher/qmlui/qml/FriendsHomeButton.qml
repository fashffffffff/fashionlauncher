import QtQuick

// "Play with friends" on Home: opens Friends; shows who's online and pending invites.
QuietButton {
    readonly property int pending: Friends.invites.filter(i => i.state === "new").length + Friends.requests.length
    iconName: "user"
    text: pending > 0 ? qsTr("Play with friends (%1)").arg(pending)
        : Friends.onlineCount > 0 ? qsTr("Play with friends · %1 online").arg(Friends.onlineCount)
        : qsTr("Play with friends")
    tip: qsTr("Play together without a server")
    onClicked: Router.go("friends")
    // Toasts for friend events. (Move to Main.qml so they show on every screen.)
    FriendsNotices {}
}
