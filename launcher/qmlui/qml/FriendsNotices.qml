import QtQuick

// Turns sidecar notices into toasts. Instantiate once in a place that is always loaded.
Item {
    visible: false
    function reasonText(r) {
        if (r === "both_strict") return qsTr("Both of your networks block direct connections. One of you can try another network, like a phone hotspot.")
        if (r === "one_strict") return qsTr("A strict network is in the way. Try again, or one of you can switch networks (a phone hotspot works).")
        return qsTr("Couldn't reach them. Ask your friend to host instead, or try again in a minute.")
    }
    Connections {
        target: Friends
        function onNotice(n) {
            const who = n.name || qsTr("Your friend")
            switch (n.kind) {
            case "request": Router.toast(qsTr("%1 wants to be friends").arg(who), false); break
            case "request_sent": Router.toast(qsTr("Friend request sent. It goes through when they're online."), false); break
            case "accepted": Router.toast(qsTr("You and %1 are now friends").arg(who), false); break
            case "declined": Router.toast(qsTr("%1 declined the friend request").arg(who), false); break
            case "removed": Router.toast(qsTr("%1 removed you from friends").arg(who), false); break
            case "already_friend": Router.toast(qsTr("%1 is already your friend").arg(who), false); break
            case "bad_code": Router.toast(qsTr("That doesn't look like a friend code."), true); break
            case "own_code": Router.toast(qsTr("That's your own code. Send it to a friend instead."), true); break
            case "invite": Router.toast(qsTr("%1 invites you to play %2").arg(who).arg(n.instance || ""), false); break
            case "invite_cancelled": Router.toast(qsTr("%1 stopped the game session").arg(who), false); break
            case "invite_accepted": Router.toast(qsTr("%1 is joining").arg(who), false); break
            case "invite_declined": Router.toast(qsTr("%1 can't join right now").arg(who), false); break
            case "world_ready": Router.toast(qsTr("%1's world is open. Join from Multiplayer or press Connect.").arg(who), false); break
            case "joined": Router.toast(qsTr("%1 joined your world").arg(who), false); break
            case "tunnel_failed": Router.toast(qsTr("Lost the connection to %1. Reconnecting…").arg(who), true); break
            case "unreachable": Router.toast(qsTr("%1: %2").arg(who).arg(reasonText(n.reason)), true); break
            }
        }
    }
}
