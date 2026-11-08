import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// "Play world…": pick a world or a server of an instance and start the game straight in it (Quick Play).
// Only what the game version supports is listed. Opened with Router.pickQuickPlay(instanceId).
ModalCard {
    id: dlg
    property string instanceId
    property var worlds: []
    property var servers: []
    property var support: ({})

    function openFor(id) {
        instanceId = id
        const i = Instances.indexOf(id)
        subtitle = i >= 0 ? Instances.get(i).name : ""
        support = QuickPlay.support(id)
        worlds = QuickPlay.worlds(id)
        servers = QuickPlay.servers(id)
        open()
    }
    function play(kind, value) {
        close()
        QuickPlay.play(instanceId, kind, value)
    }

    title: qsTr("Play world or server")
    width: Math.min(parent ? parent.width - Style.px(48) : 560, Style.px(520))

    component Entry: AbstractButton {
        id: entry
        property string iconUrl
        property string glyph
        property string label
        property string detail
        Layout.fillWidth: true
        implicitHeight: Style.px(52)
        hoverEnabled: true
        Accessible.name: label
        background: Rectangle {
            radius: Style.radius
            color: entry.down ? Style.alpha(Style.text, 0.12) : entry.hovered || entry.visualFocus ? Style.alpha(Style.text, 0.06) : "transparent"
        }
        contentItem: RowLayout {
            spacing: Style.px(12)
            Rectangle {
                Layout.leftMargin: Style.px(6)
                Layout.preferredWidth: Style.px(36)
                Layout.preferredHeight: Style.px(36)
                radius: Style.px(6)
                color: Style.alpha(Style.text, 0.06)
                clip: true
                Image { anchors.fill: parent; source: entry.iconUrl; visible: !!entry.iconUrl; smooth: false; fillMode: Image.PreserveAspectCrop; asynchronous: true }
                Icon { anchors.centerIn: parent; visible: !entry.iconUrl; name: entry.glyph; color: Style.textMuted; size: Style.px(18) }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Txt { text: entry.label; Layout.fillWidth: true; font.weight: Font.Medium }
                Txt { text: entry.detail; visible: text.length > 0; Layout.fillWidth: true; muted: true; font.pixelSize: Style.fontSmall }
            }
            Icon { Layout.rightMargin: Style.px(8); name: "play"; size: Style.px(14); color: entry.hovered ? Style.text : Style.textMuted }
        }
    }

    Flickable {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(list.implicitHeight, Style.px(420))
        contentHeight: list.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: list
            width: parent.width
            spacing: Style.px(2)

            Txt { visible: dlg.worlds.length > 0; text: qsTr("Worlds"); muted: true; font.weight: Font.DemiBold; Layout.leftMargin: Style.px(6) }
            Repeater {
                model: dlg.worlds
                delegate: Entry {
                    required property var modelData
                    iconUrl: modelData.icon
                    glyph: "world"
                    label: modelData.name
                    detail: Fmt.lastPlayed(modelData.lastPlayed)
                    onClicked: dlg.play("world", modelData.folder)
                }
            }
            Item { visible: dlg.worlds.length > 0 && dlg.servers.length > 0; implicitHeight: Style.px(8) }
            Txt { visible: dlg.servers.length > 0; text: qsTr("Servers"); muted: true; font.weight: Font.DemiBold; Layout.leftMargin: Style.px(6) }
            Repeater {
                model: dlg.servers
                delegate: Entry {
                    required property var modelData
                    iconUrl: modelData.icon
                    glyph: "globe"
                    label: modelData.name || modelData.address
                    detail: modelData.address
                    onClicked: dlg.play("server", modelData.address)
                }
            }
            Txt {
                visible: dlg.worlds.length === 0 && dlg.servers.length === 0
                Layout.fillWidth: true
                text: dlg.support.world ? qsTr("No worlds or servers yet. Create a world or add a server in the game first.")
                                        : qsTr("This Minecraft version can only join servers directly, and none is saved yet. Add a server in the game first.")
                muted: true
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        QuietButton { text: qsTr("Cancel"); onClicked: dlg.close() }
    }
}
