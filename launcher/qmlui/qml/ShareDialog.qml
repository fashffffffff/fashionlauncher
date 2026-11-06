import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Share instance": pick what to send (server-side mods or everything), untick mods, then copy a link or save a file.
// Usage: ShareDialog { instanceId: … } then open(); or ShareDialog.qml created on the fly with instanceId set.
Popup {
    id: dlg
    property string instanceId
    property bool includeClient: Sharing.includeClientDefault
    property var exclude: []
    property var full: ({})          // everything, before the server-only filter
    property string copiedLink: ""
    property bool destroyOnClose: false
    readonly property var shown: Sharing.filterManifest(full, { includeClient: includeClient })
    readonly property var chosen: Sharing.filterManifest(full, { includeClient: includeClient, exclude: exclude })
    readonly property int localCount: (chosen.mods || []).concat(chosen.resourcepacks || [], chosen.shaderpacks || []).filter(m => m.platform === "local").length
    readonly property int count: (chosen.mods || []).length + (chosen.resourcepacks || []).length + (chosen.shaderpacks || []).length

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Style.px(620), parent ? parent.width - Style.px(60) : Style.px(620))
    height: Math.min(Style.px(720), parent ? parent.height - Style.px(60) : Style.px(720))
    modal: true
    padding: Style.px(24)
    background: Rectangle { radius: Style.radius + Style.px(4); color: Style.surface; border.width: 1; border.color: Style.line }
    Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }

    function reload() { full = Sharing.createManifest(instanceId, { includeClient: true }) }
    onAboutToShow: { copiedLink = ""; exclude = []; reload(); Sharing.analyze(instanceId) }
    onClosed: if (destroyOnClose) destroy()
    Connections {
        target: Sharing
        function onAnalyzed(id) { if (id === dlg.instanceId && dlg.opened) dlg.reload() }
    }

    contentItem: ColumnLayout {
        spacing: Style.px(14)

        RowLayout {
            Layout.fillWidth: true
            Txt {
                Layout.fillWidth: true
                text: qsTr("Share “%1”").arg(Sharing.instanceName(dlg.instanceId))
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            QuietButton { plain: true; iconName: "close"; tip: qsTr("Close"); onClicked: dlg.close() }
        }
        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            muted: true
            text: qsTr("Friends paste the link in Fashion Launcher and get Minecraft %1%2 with the same mods, downloaded from Modrinth and CurseForge. Worlds and settings aren't sent.")
                  .arg(dlg.full.mc || "").arg(dlg.full.loader ? " · " + dlg.full.loader.charAt(0).toUpperCase() + dlg.full.loader.slice(1) : "")
        }

        SettingsSegmented {
            options: [{ value: false, label: qsTr("Server-side mods only") }, { value: true, label: qsTr("Everything") }]
            value: dlg.includeClient
            onPicked: (v) => { dlg.includeClient = v; Sharing.includeClientDefault = v; dlg.copiedLink = "" }
        }
        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSmall
            muted: true
            text: dlg.includeClient
                  ? qsTr("All your mods, resource packs and shader packs — a full copy of how you play.")
                  : qsTr("Only what a server needs, so friends can join or host it. Client-only extras like minimaps, shaders and resource packs stay with you.")
        }

        ShareModList {
            id: mods
            Layout.fillWidth: true
            Layout.fillHeight: true
            manifest: dlg.shown
            loading: Sharing.analyzing && dlg.count === 0
            exclude: dlg.exclude
            onEdited: (e) => { dlg.exclude = e; dlg.copiedLink = "" }
        }

        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSmall
            color: dlg.localCount ? Style.danger : Style.textMuted
            text: {
                if (Sharing.analyzing) return qsTr("Looking your mods up on Modrinth…")
                let s = qsTr("%n item(s) will be shared.", "", dlg.count)
                if (dlg.localCount) s += " " + qsTr("%n of them can't be downloaded by others — send those files separately or untick them.", "", dlg.localCount)
                if (dlg.copiedLink.length > 2000) s += " " + qsTr("The link is long and some chats cut it off; the file is safer.")
                return s
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(10)
            Txt {
                Layout.fillWidth: true
                visible: dlg.copiedLink.length > 0
                text: qsTr("Link copied")
                color: Style.success
            }
            Item { Layout.fillWidth: true; visible: dlg.copiedLink.length === 0 }
            QuietButton {
                iconName: "export"
                text: qsTr("Save file")
                onClicked: {
                    const path = Sharing.saveFile(dlg.chosen)
                    if (path) Router.toast(qsTr("Saved %1").arg(path.split("/").pop()))
                    else if (Sharing.error) { Router.toast(Sharing.error, true); Sharing.clearError() }
                }
            }
            PrimaryButton {
                implicitHeight: Style.px(42)
                iconName: dlg.copiedLink ? "check" : "link"
                text: qsTr("Copy link")
                onClicked: {
                    dlg.copiedLink = Sharing.packLink(dlg.chosen)
                    Sharing.copyText(dlg.copiedLink)
                    Router.toast(qsTr("Share link copied — send it to your friends"))
                }
            }
        }
    }
}
