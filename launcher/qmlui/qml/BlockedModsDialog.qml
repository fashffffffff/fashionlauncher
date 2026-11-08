import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// CurseForge mods whose authors don't allow launchers to download them: the pack install waits
// while you fetch them by hand. Files that appear in Downloads (or any folder added below) are
// recognised by their hash and ticked automatically; a file can also be picked by hand.
ModalCard {
    id: dlg
    dismissable: false
    width: Math.min(parent ? parent.width - Style.px(48) : 720, Style.px(720))
    height: Math.min(parent ? parent.height - Style.px(48) : 660, Math.max(Style.px(430), implicitHeight))

    property int pickIndex: -1

    title: qsTr("Some mods need a manual download")
    subtitle: qsTr("The makers of these mods don't allow launchers to download them. Open each page, download the file, and it is found here automatically as soon as it arrives in your Downloads folder — or pick the file yourself. Press Continue when everything is found.")

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(contentHeight, Style.px(340))
        clip: true
        spacing: Style.px(8)
        boundsBehavior: Flickable.StopAtBounds
        model: BlockedMods.mods
        ScrollBar.vertical: ScrollBar { }

        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            readonly property var m: modelData
            width: ListView.view.width
            implicitHeight: rowCol.implicitHeight + Style.px(24)
            radius: Style.radius
            color: Style.alpha(Style.surfaceHi, 0.5)
            border.width: 1
            border.color: m.found ? Style.success : Style.line

            Accessible.role: Accessible.ListItem
            Accessible.name: m.name + " " + (m.found ? qsTr("Found") : qsTr("Waiting"))

            ColumnLayout {
                id: rowCol
                anchors { fill: parent; margins: Style.px(10) }
                spacing: Style.px(6)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(8)
                    Txt {
                        Layout.fillWidth: true
                        text: row.m.name
                        font.weight: Font.Medium
                        elide: Text.ElideMiddle
                    }
                    Txt {
                        text: row.m.found ? qsTr("Found ✓") : qsTr("Waiting")
                        color: row.m.found ? Style.success : Style.textMuted
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.Medium
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(8)
                    Txt {
                        Layout.fillWidth: true
                        text: row.m.found ? row.m.localPath : qsTr("Hash: %1").arg(row.m.hash)
                        muted: true
                        font.pixelSize: Style.fontSmall
                        elide: Text.ElideMiddle
                    }
                    QuietButton { iconName: "link"; text: qsTr("Open page"); onClicked: BlockedMods.openPage(row.index) }
                    QuietButton {
                        visible: !row.m.found
                        text: qsTr("Pick file…")
                        onClicked: { dlg.pickIndex = row.index; fileDialog.open() }
                    }
                }
            }
        }
    }

    Txt {
        Layout.fillWidth: true
        text: qsTr("%1 of %2 found").arg(BlockedMods.foundCount).arg(BlockedMods.mods.length)
        muted: true
    }

    // Two rows so the buttons never push the card wider than the window
    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(8)
        QuietButton { text: qsTr("Open all pages"); onClicked: BlockedMods.openAll() }
        QuietButton { iconName: "folder"; text: qsTr("Add folder…"); onClicked: folderDialog.open() }
        Item { Layout.fillWidth: true }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(8)
        QuietButton { text: qsTr("Cancel"); onClicked: BlockedMods.cancel() }
        Item { Layout.fillWidth: true }
        QuietButton { text: qsTr("Continue without them"); onClicked: BlockedMods.confirm() }
        PrimaryButton {
            text: qsTr("Continue")
            enabled: BlockedMods.allFound
            onClicked: BlockedMods.confirm()
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose the folder you downloaded the mods to")
        onAccepted: BlockedMods.addFolder(selectedFolder)
    }
    FileDialog {
        id: fileDialog
        title: qsTr("Choose the downloaded file")
        fileMode: FileDialog.OpenFile
        onAccepted: BlockedMods.pickFile(dlg.pickIndex, selectedFile)
    }
}
