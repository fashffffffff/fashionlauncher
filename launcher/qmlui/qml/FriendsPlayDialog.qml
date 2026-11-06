import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Host: pick an instance, what to share, and which friends to invite.
Popup {
    id: dlg
    property string instanceId: ""
    property var preselect: []              // friend keys
    property var picked: []
    property var fullManifest: null

    readonly property bool sharing: typeof Sharing !== "undefined" && !!Sharing && typeof Sharing.createManifest === "function"
    readonly property var inst: { Instances.count; const r = Instances.indexOf(instanceId); return r >= 0 ? Instances.get(r) : null }
    readonly property var reachable: Friends.friends.filter(f => !f.pending && f.conn === "direct")

    function openFor(id, keys) {
        instanceId = id || Router.selectedId || (Instances.count ? Instances.get(0).instanceId : "")
        preselect = keys || []
        picked = preselect.slice()
        refreshManifest()
        open()
    }
    property bool analyzing: false
    function refreshManifest() {
        fullManifest = null
        if (!sharing || !instanceId.length) return
        // mod sides/platforms are only known after analyze(); the manifest is built when it reports back
        if (typeof Sharing.analyze === "function") { analyzing = true; Sharing.analyze(instanceId) }
        else buildManifest()
    }
    function buildManifest() {
        analyzing = false
        const m = Sharing.createManifest(instanceId, { includeClient: true, exclude: [] })
        fullManifest = m && typeof m === "object" ? m : null
    }
    Connections {
        target: dlg.sharing ? Sharing : null
        ignoreUnknownSignals: true
        function onAnalyzed(id) { if (dlg.analyzing && id === dlg.instanceId) dlg.buildManifest() }
    }
    function send() {
        let manifest = null
        if (sharing) {
            const m = Sharing.createManifest(instanceId, { includeClient: packOpts.includeClient, exclude: packOpts.exclude })
            manifest = m && typeof m === "object" ? m : null
        }
        Friends.invite(picked, { instanceId: instanceId, instance: inst ? inst.name : "", mc: inst ? inst.gameVersion : "", manifest: manifest })
        Router.toast(qsTr("Invite sent. Start the game and open your world to LAN."), false)
        if (inst && !inst.running) Launcher.launch(instanceId)
        close()
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

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            id: content
            width: parent.width
            spacing: Style.px(14)

            Txt { text: qsTr("Play together"); font.family: Style.display; font.pixelSize: Style.fontTitle }

            Txt { text: qsTr("1. What do you want to play?"); font.weight: Font.DemiBold }
            SettingsDropdown {
                Layout.fillWidth: true
                value: dlg.instanceId
                options: {
                    Instances.count
                    const out = []
                    for (let i = 0; i < Instances.count; i++) {
                        const it = Instances.get(i)
                        out.push({ value: it.instanceId, label: it.name + (it.gameVersion ? "  ·  " + it.gameVersion : "") })
                    }
                    return out
                }
                onPicked: v => { dlg.instanceId = v; dlg.refreshManifest() }
            }

            Txt { text: qsTr("2. What do friends get?"); font.weight: Font.DemiBold }
            Txt { visible: dlg.analyzing; muted: true; text: qsTr("Checking the mods…") }
            FriendsPackOptions {
                id: packOpts
                Layout.fillWidth: true
                visible: dlg.sharing
                manifest: dlg.fullManifest
            }
            Txt {
                visible: !dlg.sharing
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: qsTr("Pack sharing isn't available in this build yet, so friends join with an instance of their own (same Minecraft version and mods).")
            }

            Txt { text: qsTr("3. Who's invited?"); font.weight: Font.DemiBold }
            Txt {
                visible: dlg.reachable.length === 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: Friends.friends.length === 0 ? qsTr("Add a friend first: send them your code.")
                                                   : qsTr("None of your friends are connected right now. Invites need both of you online.")
            }
            Flow {
                Layout.fillWidth: true
                spacing: Style.px(6)
                Repeater {
                    model: dlg.reachable
                    delegate: CreateChip {
                        text: modelData.name
                        checked: dlg.picked.indexOf(modelData.key) >= 0
                        onToggled: {
                            const p = dlg.picked.filter(k => k !== modelData.key)
                            if (checked) p.push(modelData.key)
                            dlg.picked = p
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Style.px(6)
                spacing: Style.px(8)
                Item { Layout.fillWidth: true }
                QuietButton { text: qsTr("Cancel"); onClicked: dlg.close() }
                PrimaryButton {
                    text: dlg.picked.length > 1 ? qsTr("Invite %n friends", "", dlg.picked.length) : qsTr("Send invite")
                    iconName: "play"
                    enabled: !!dlg.inst && dlg.picked.length > 0 && !dlg.analyzing
                    onClicked: dlg.send()
                }
            }
        }
    }
}
