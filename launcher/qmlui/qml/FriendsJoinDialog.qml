import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Joiner: choose how to join a friend's game (matching instance, new copy of the pack, or any instance).
Popup {
    id: dlg
    property var invite: ({})
    property string choice: ""              // "use:<id>" | "new" | "own"
    property string ownId: ""
    property var matches: []
    property string pendingInvite: ""       // waiting for Sharing.applied

    readonly property bool sharing: typeof Sharing !== "undefined" && !!Sharing && typeof Sharing.applyManifest === "function"
    readonly property bool hasPack: sharing && !!invite.manifest && !!invite.manifest.mods

    function openFor(inv) {
        invite = inv
        let m = []
        if (sharing && typeof Sharing.matchInstances === "function" && inv.manifest) {
            const r = Sharing.matchInstances(inv.manifest)
            if (Array.isArray(r)) m = r.map(x => ({ instanceId: x.instanceId, name: x.name,
                                                    missing: typeof x.missingCount === "number" ? x.missingCount
                                                           : Array.isArray(x.missing) ? x.missing.length : -1 }))
        } else {
            for (let i = 0; i < Instances.count; i++) {
                const it = Instances.get(i)
                if (inv.mc && it.gameVersion === inv.mc) m.push({ instanceId: it.instanceId, name: it.name, missing: -1 })
            }
        }
        matches = m.slice(0, 4)
        choice = matches.length ? "use:" + matches[0].instanceId : hasPack ? "new" : "own"
        ownId = Router.selectedId
        open()
    }
    function join() {
        const inv = invite
        Friends.replyInvite(inv.from, inv.id, true)
        if (choice.startsWith("use:")) {
            const id = choice.slice(4)
            const m = matches.find(x => x.instanceId === id)
            Friends.setJoinInstance(inv.id, id)
            if (hasPack && m && m.missing > 0) {
                pendingInvite = inv.id
                Sharing.applyManifest(inv.manifest, { targetInstanceId: id, includeClient: false, exclude: [] })
            }
        } else if (choice === "new") {
            pendingInvite = inv.id
            Sharing.applyManifest(inv.manifest, { includeClient: packOpts.includeClient, exclude: packOpts.exclude,
                                                  name: qsTr("%1 (with %2)").arg(inv.instance).arg(inv.name) })
            Router.toast(qsTr("Installing the pack… You can connect once it's ready."), false)
        } else {
            Friends.setJoinInstance(inv.id, ownId)
        }
        close()
    }

    Connections {
        target: dlg.sharing ? Sharing : null
        ignoreUnknownSignals: true
        function onApplied(instanceId) {
            if (!dlg.pendingInvite.length) return
            Friends.setJoinInstance(dlg.pendingInvite, instanceId)
            dlg.pendingInvite = ""
            Router.toast(qsTr("The pack is ready. Press Connect when the world is open."), false)
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Style.px(560), parent ? parent.width - Style.px(48) : Style.px(560))
    height: Math.min(content.implicitHeight + Style.px(48), parent ? parent.height - Style.px(48) : Style.px(640))
    padding: Style.px(24)
    modal: true
    focus: true
    background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
    Overlay.modal: Rectangle { color: Style.scrim }

    component Choice: AbstractButton {
        id: ch
        property string key
        property string detail
        readonly property bool on: dlg.choice === key
        Layout.fillWidth: true
        implicitHeight: chCol.implicitHeight + Style.px(20)
        hoverEnabled: true
        onClicked: dlg.choice = key
        background: Rectangle {
            radius: Style.radius
            color: ch.on ? Style.alpha(Style.accent, 0.16) : ch.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
            border.width: ch.on ? 2 : 1
            border.color: ch.on ? Style.accent : Style.line
        }
        contentItem: ColumnLayout {
            id: chCol
            spacing: Style.px(2)
            Item { Layout.fillHeight: true }
            Txt { text: ch.text; font.weight: Font.DemiBold; Layout.fillWidth: true; Layout.leftMargin: Style.px(12) }
            Txt { visible: ch.detail.length > 0; text: ch.detail; muted: true; font.pixelSize: Style.fontSmall; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: Style.px(12); Layout.rightMargin: Style.px(12) }
            Item { Layout.fillHeight: true }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            id: content
            width: parent.width
            spacing: Style.px(12)

            Txt { text: qsTr("Join %1").arg(dlg.invite.name || ""); font.family: Style.display; font.pixelSize: Style.fontTitle }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: qsTr("%1 is playing %2%3.").arg(dlg.invite.name || "").arg(dlg.invite.instance || "")
                      .arg(dlg.invite.mc ? qsTr(" on Minecraft %1").arg(dlg.invite.mc) : "")
            }

            Repeater {
                model: dlg.matches
                delegate: Choice {
                    key: "use:" + modelData.instanceId
                    text: qsTr("Use %1").arg(modelData.name)
                    detail: modelData.missing > 0 ? qsTr("Adds %n missing server mod(s)", "", modelData.missing)
                          : modelData.missing === 0 ? qsTr("Has everything needed")
                          : qsTr("Same Minecraft version")
                }
            }
            Choice {
                visible: dlg.hasPack
                key: "new"
                text: qsTr("Install the pack as a new instance")
                detail: qsTr("Gets the mods from your friend's pack. Choose how much below.")
            }
            FriendsPackOptions {
                id: packOpts
                visible: dlg.choice === "new"
                Layout.fillWidth: true
                Layout.leftMargin: Style.px(12)
                manifest: dlg.invite.manifest || null
            }
            Choice {
                key: "own"
                text: qsTr("Join with an instance I pick")
                detail: qsTr("Nothing gets installed. It needs the same Minecraft version and mods.")
            }
            SettingsDropdown {
                visible: dlg.choice === "own"
                Layout.fillWidth: true
                Layout.leftMargin: Style.px(12)
                value: dlg.ownId
                options: {
                    Instances.count
                    const out = []
                    for (let i = 0; i < Instances.count; i++) {
                        const it = Instances.get(i)
                        out.push({ value: it.instanceId, label: it.name + (it.gameVersion ? "  ·  " + it.gameVersion : "") })
                    }
                    return out
                }
                onPicked: v => dlg.ownId = v
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Style.px(6)
                spacing: Style.px(8)
                Item { Layout.fillWidth: true }
                QuietButton { text: qsTr("Cancel"); onClicked: dlg.close() }
                PrimaryButton {
                    text: qsTr("Join"); iconName: "play"
                    enabled: dlg.choice !== "own" || dlg.ownId.length > 0
                    onClicked: dlg.join()
                }
            }
        }
    }
}
