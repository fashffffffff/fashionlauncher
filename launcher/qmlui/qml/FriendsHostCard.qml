import QtQuick
import QtQuick.Layouts

// Your play-together session as the host: who's invited and whether the world is open.
Rectangle {
    id: card
    property var session: ({})
    readonly property var world: Friends.hosting.world || null
    readonly property string instanceId: session.instanceId || ""
    readonly property var inst: { Instances.count; const r = Instances.indexOf(instanceId); return r >= 0 ? Instances.get(r) : null }

    implicitHeight: col.implicitHeight + Style.px(36)
    radius: Style.radius
    color: Style.panel(1)
    border.width: 2
    border.color: Style.accent

    function stateText(s) {
        if (s === "playing") return qsTr("In your world")
        if (s === "accepted") return qsTr("Joining")
        if (s === "declined") return qsTr("Can't join")
        return qsTr("Invited")
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(18) }
        spacing: Style.px(10)

        Txt { text: qsTr("You're hosting %1").arg(card.session.instance || ""); font.family: Style.display; font.pixelSize: Style.fontLead; Layout.fillWidth: true }
        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            muted: !card.world
            text: card.world
                ? qsTr("Your world “%1” is open. Friends who joined can see it now.").arg(card.world.motd)
                : qsTr("Start the game, load a world, then press Esc → Open to LAN. Friends get it automatically.")
        }
        Repeater {
            model: card.session.to || []
            delegate: RowLayout {
                spacing: Style.px(10)
                Layout.fillWidth: true
                FriendsAvatar { implicitWidth: Style.px(28); name: modelData.name; showDot: false }
                Txt { text: modelData.name; Layout.fillWidth: true }
                Txt { text: card.stateText(modelData.state); muted: modelData.state !== "playing"; color: modelData.state === "playing" ? Style.success : Style.textMuted }
            }
        }
        RowLayout {
            spacing: Style.px(8)
            PrimaryButton {
                visible: !!card.inst
                text: card.inst && card.inst.running ? qsTr("Game is running") : qsTr("Start %1").arg(card.inst ? card.inst.name : "")
                iconName: "play"
                implicitHeight: Style.px(40)
                enabled: !!card.inst && !card.inst.running
                onClicked: Launcher.launch(card.instanceId)
            }
            QuietButton { text: qsTr("Stop hosting"); danger: true; onClicked: Friends.cancelInvite(card.session.id) }
        }
    }
}
