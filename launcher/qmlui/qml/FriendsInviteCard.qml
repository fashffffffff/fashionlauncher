import QtQuick
import QtQuick.Layouts

// An invite from a friend: before joining (Join / Decline) and after (status + Connect).
Rectangle {
    id: card
    property var invite: ({})
    signal joinRequested(var invite)

    readonly property bool joined: invite.state === "accepted"
    readonly property var world: invite.world || null
    readonly property string chosenId: { Friends.invites; return Friends.joinInstance(invite.id || "") }
    readonly property var chosen: { Instances.count; const r = Instances.indexOf(chosenId); return r >= 0 ? Instances.get(r) : null }
    readonly property int modCount: invite.manifest && invite.manifest.mods ? invite.manifest.mods.length : 0

    implicitHeight: col.implicitHeight + Style.px(36)
    radius: Style.radius
    color: Style.panel(1)
    border.width: joined ? 2 : 1
    border.color: joined ? Style.accent : Style.panelBorder

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(18) }
        spacing: Style.px(10)

        RowLayout {
            spacing: Style.px(12)
            Layout.fillWidth: true
            FriendsAvatar { name: card.invite.name || ""; online: !!card.invite.connected }
            ColumnLayout {
                spacing: Style.px(2)
                Layout.fillWidth: true
                Txt {
                    Layout.fillWidth: true
                    text: card.joined ? qsTr("Playing with %1").arg(card.invite.name) : qsTr("%1 invites you to play").arg(card.invite.name)
                    font.weight: Font.DemiBold
                }
                Txt {
                    Layout.fillWidth: true
                    muted: true
                    font.pixelSize: Style.fontSmall
                    text: [card.invite.instance,
                           card.invite.mc ? qsTr("Minecraft %1").arg(card.invite.mc) : "",
                           card.modCount > 0 ? qsTr("%n mod(s)", "", card.modCount) : ""].filter(s => s).join(" · ")
                }
            }
        }

        Txt {
            visible: card.joined
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: {
                if (!card.invite.connected) return qsTr("Reconnecting to %1…").arg(card.invite.name)
                if (!card.world) return qsTr("Waiting for %1 to open the world. In the game: Esc → Open to LAN.").arg(card.invite.name)
                return qsTr("Ready! Open Multiplayer in Minecraft — “%1” is in the LAN list. Or connect right away.").arg(card.world.motd)
            }
        }

        RowLayout {
            spacing: Style.px(8)
            Layout.fillWidth: true
            visible: !card.joined
            PrimaryButton {
                text: qsTr("Join…"); iconName: "play"
                implicitHeight: Style.px(40)
                enabled: !!card.invite.connected
                onClicked: card.joinRequested(card.invite)
            }
            QuietButton { text: qsTr("Decline"); onClicked: Friends.replyInvite(card.invite.from, card.invite.id, false) }
            Txt { visible: !card.invite.connected; muted: true; text: qsTr("Connecting…") }
        }

        RowLayout {
            spacing: Style.px(8)
            Layout.fillWidth: true
            visible: card.joined
            SettingsDropdown {
                id: pick
                Layout.fillWidth: true
                Layout.minimumWidth: Style.px(120)
                Layout.maximumWidth: Style.px(260)
                placeholder: qsTr("Instance to play with")
                value: card.chosenId
                options: {
                    Instances.count
                    const out = []
                    for (let i = 0; i < Instances.count; i++) {
                        const it = Instances.get(i)
                        out.push({ value: it.instanceId, label: it.name + (it.gameVersion ? "  ·  " + it.gameVersion : "") })
                    }
                    return out
                }
                onPicked: v => Friends.setJoinInstance(card.invite.id, v)
            }
            PrimaryButton {
                text: qsTr("Connect"); iconName: "play"
                implicitHeight: Style.px(40)
                implicitWidth: Style.px(130)
                enabled: !!card.world && !!card.invite.connected && card.chosenId.length > 0 && card.invite.port > 0
                         && !(card.chosen && card.chosen.running)
                onClicked: {
                    if (Friends.connectInstance(card.chosenId, card.invite.port))
                        Router.toast(qsTr("Starting %1 and joining %2's world").arg(card.chosen ? card.chosen.name : "").arg(card.invite.name), false)
                    else Router.toast(qsTr("Couldn't start that instance."), true)
                }
            }
            QuietButton { text: qsTr("Leave"); onClicked: Friends.replyInvite(card.invite.from, card.invite.id, false) }
        }
        Txt {
            visible: card.joined && typeof Sharing !== "undefined" && !!Sharing.busy
            Layout.fillWidth: true
            muted: true
            font.pixelSize: Style.fontSmall
            text: visible ? (Sharing.status || qsTr("Getting the pack ready…")) : ""
        }
    }
}
