import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FFPrism
import "Fmt.js" as Fmt

// Worlds of the instance: jump straight into one, or rename, copy and delete.
Item {
    id: tab
    property string instanceId
    property var instance

    WorldsModel {
        id: worlds
        instanceId: tab.instanceId
        onFailed: (reason) => Router.toast(reason, true)
    }

    function sizeText(bytes) {
        if (bytes < 1024 * 1024) return qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)))
        if (bytes < 1024 * 1024 * 1024) return qsTr("%1 MB").arg((bytes / 1048576).toFixed(1))
        return qsTr("%1 GB").arg((bytes / 1073741824).toFixed(2))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(14)
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Txt { text: worlds.busy ? qsTr("Working…") : qsTr("%n world(s)", "", worlds.count); muted: true; Layout.fillWidth: true }
            QuietButton { iconName: "refresh"; text: qsTr("Refresh"); onClicked: worlds.refresh() }
            QuietButton { iconName: "folder"; text: qsTr("Open saves folder"); onClicked: worlds.openWorldsFolder() }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Style.px(8)
            model: worlds
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                width: list.width - Style.px(8)
                height: Style.px(72)
                radius: Style.radius
                color: Style.panel(1)
                border.width: 1
                border.color: Style.panelBorder
                property bool renaming: false

                RowLayout {
                    anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                    spacing: Style.px(14)
                    Rectangle {
                        Layout.preferredWidth: Style.px(48); Layout.preferredHeight: Style.px(48)
                        radius: Style.px(6)
                        color: Style.alpha(Style.text, 0.06)
                        clip: true
                        Image { anchors.fill: parent; source: model.icon; visible: !!model.icon; smooth: false; sourceSize: Qt.size(96, 96) }
                        Icon { anchors.centerIn: parent; visible: !model.icon; name: "world"; color: Style.textMuted }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(3)
                        Txt { visible: !row.renaming; text: model.name; font.weight: Font.Medium; font.pixelSize: Style.fontLead; Layout.fillWidth: true }
                        Field {
                            id: renameField
                            visible: row.renaming
                            Layout.fillWidth: true
                            text: model.name
                            onAccepted: { worlds.rename(index, text); row.renaming = false }
                            Keys.onEscapePressed: row.renaming = false
                        }
                        Txt {
                            Layout.fillWidth: true
                            muted: true
                            text: [model.mode, Fmt.lastPlayed(model.lastPlayed), tab.sizeText(model.size)].filter(s => s).join("     ")
                        }
                    }
                    PrimaryButton {
                        implicitHeight: Style.px(40)
                        implicitWidth: Style.px(120)
                        iconName: "play"
                        text: qsTr("Play")
                        onClicked: worlds.play(index)
                    }
                    QuietButton {
                        iconName: "more"
                        tip: qsTr("More actions")
                        onClicked: worldMenu.popup()
                        ThemedMenu {
                            id: worldMenu
                            ThemedMenuItem { text: qsTr("Rename"); iconName: "pencil"; onTriggered: { row.renaming = true; renameField.forceActiveFocus(); renameField.selectAll() } }
                            ThemedMenuItem { text: qsTr("Make a copy"); iconName: "copy"; onTriggered: worlds.duplicate(index, qsTr("%1 copy").arg(model.name)) }
                            ThemedMenuItem { text: qsTr("Copy seed"); iconName: "copy"; onTriggered: { worlds.copySeed(index); Router.toast(qsTr("Seed copied")) } }
                            ThemedMenuItem { text: qsTr("Open folder"); iconName: "folder"; onTriggered: worlds.openFolder(index) }
                            ThemedMenuItem { text: qsTr("Delete"); iconName: "trash"; danger: true; onTriggered: confirm.ask(index, model.name) }
                        }
                    }
                }
            }
            Txt {
                anchors.centerIn: parent
                visible: worlds.count === 0
                text: qsTr("No worlds yet. Play once and create a world in the game.")
                muted: true
            }
        }
    }

    // Deleting a world can't be undone, so ask first.
    Popup {
        id: confirm
        property int row: -1
        property string name
        function ask(r, n) { row = r; name = n; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Style.px(420)
        modal: true
        padding: Style.px(22)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }
        contentItem: ColumnLayout {
            spacing: Style.px(14)
            Txt { text: qsTr("Delete “%1”?").arg(confirm.name); font.pixelSize: Style.fontLead; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Txt { text: qsTr("The world and everything built in it will be deleted. This can't be undone."); muted: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Style.px(8)
                QuietButton { text: qsTr("Cancel"); onClicked: confirm.close() }
                PrimaryButton { text: qsTr("Delete world"); fill: Style.danger; ink: "#FFFFFF"; implicitHeight: Style.px(40); onClicked: { worlds.remove(confirm.row); confirm.close() } }
            }
        }
    }
}
