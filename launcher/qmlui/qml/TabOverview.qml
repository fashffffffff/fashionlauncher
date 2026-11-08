import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// Name, icon and everyday actions for one instance.
Item {
    id: tab
    property string instanceId
    property var instance

    component Action: QuietButton { Layout.fillWidth: true; implicitHeight: Style.px(44) }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: Math.min(parent.width, Style.px(720))
            spacing: Style.px(14)

            Txt { text: qsTr("Name"); font.weight: Font.DemiBold }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(10)
                Field {
                    id: nameField
                    Layout.fillWidth: true
                    text: tab.instance ? tab.instance.name : ""
                    onAccepted: Launcher.renameInstance(tab.instanceId, text)
                }
                QuietButton {
                    text: qsTr("Rename")
                    enabled: tab.instance && nameField.text.trim().length > 0 && nameField.text !== tab.instance.name
                    onClicked: Launcher.renameInstance(tab.instanceId, nameField.text)
                }
                QuietButton { iconName: "image"; text: qsTr("Change icon"); onClicked: Launcher.changeIcon(tab.instanceId) }
            }

            Txt { text: qsTr("Actions"); font.weight: Font.DemiBold; Layout.topMargin: Style.px(8) }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                uniformCellWidths: true
                columnSpacing: Style.px(10)
                rowSpacing: Style.px(10)
                Action { iconName: "folder"; text: qsTr("Open folder"); onClicked: Launcher.openInstanceFolder(tab.instanceId) }
                Action { iconName: "terminal"; text: qsTr("Game log"); onClicked: Launcher.showConsole(tab.instanceId) }
                Action { iconName: "link"; text: qsTr("Share game log"); onClicked: shareConfirm.open() }
                Action {
                    iconName: "globe"; text: qsTr("Share instance")
                    onClicked: Qt.createComponent("ShareDialog.qml").createObject(tab, { instanceId: tab.instanceId, destroyOnClose: true }).open()
                }
                Action { iconName: "play"; text: qsTr("Play offline"); onClicked: Launcher.launch(tab.instanceId, true) }
                Action { iconName: "copy"; text: qsTr("Copy instance"); onClicked: Launcher.copyInstance(tab.instanceId) }
                Action { iconName: "sliders"; text: qsTr("Copy game settings"); onClicked: Router.copyGameOptions({ source: tab.instanceId }) }
                Action { iconName: "export"; text: qsTr("Export"); onClicked: Launcher.exportInstance(tab.instanceId) }
                Action { iconName: "cube"; text: qsTr("Move to another Minecraft version…"); onClicked: Router.open("upgrade", { instanceId: tab.instanceId }) }
                Action { iconName: "trash"; text: qsTr("Delete"); danger: true; onClicked: Launcher.deleteInstance(tab.instanceId) }
            }

            Txt { text: qsTr("Version"); font.weight: Font.DemiBold; Layout.topMargin: Style.px(8) }
            RowLayout {
                spacing: Style.px(12)
                Txt { text: Fmt.versionLine(tab.instance) || qsTr("Unknown version"); font.pixelSize: Style.fontLead }
                QuietButton { text: qsTr("Change version or loader"); onClicked: Router.open("version", { instanceId: tab.instanceId }) }
            }

            Txt {
                visible: !!(tab.instance && tab.instance.notes)
                text: qsTr("Notes"); font.weight: Font.DemiBold; Layout.topMargin: Style.px(8)
            }
            Txt {
                visible: !!(tab.instance && tab.instance.notes)
                text: tab.instance ? tab.instance.notes : ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                muted: true
            }
        }
    }

    // Uploading a log is outward-facing, so say where it goes and ask first.
    Popup {
        id: shareConfirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Style.px(460)
        modal: true
        padding: Style.px(22)
        readonly property string file: { opened; return Launcher.latestLogFile(tab.instanceId) }
        onAboutToShow: Launcher.resetLogShare()
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }
        contentItem: ColumnLayout {
            spacing: Style.px(12)
            Txt { text: qsTr("Share the game log?"); font.pixelSize: Style.fontLead; font.weight: Font.DemiBold }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: shareConfirm.file
                    ? qsTr("“%1” will be uploaded to %2 and the link copied, so you can send it to someone who helps you. Folder paths with your user name and login tokens are hidden.")
                          .arg(shareConfirm.file.split("/").pop()).arg(Launcher.pasteHost())
                    : qsTr("There is no game log yet. Play once first.")
            }
            Txt {
                visible: Launcher.logShare.state === "done" || Launcher.logShare.state === "failed"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Launcher.logShare.state === "failed" ? Style.danger : Style.success
                text: Launcher.logShare.state === "done" ? qsTr("Link copied: %1").arg(Launcher.logShare.url) : (Launcher.logShare.error || "")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Style.px(8)
                QuietButton { text: Launcher.logShare.state === "done" ? qsTr("Close") : qsTr("Cancel"); onClicked: shareConfirm.close() }
                QuietButton {
                    visible: Launcher.logShare.state === "done"
                    iconName: "link"; text: qsTr("Open link")
                    onClicked: Launcher.openUrl(Launcher.logShare.url)
                }
                PrimaryButton {
                    visible: !!shareConfirm.file && Launcher.logShare.state !== "done"
                    implicitHeight: Style.px(40)
                    text: Launcher.logShare.state === "uploading" ? qsTr("Uploading…") : qsTr("Upload and copy link")
                    enabled: Launcher.logShare.state !== "uploading"
                    onClicked: Launcher.shareLatestLog(tab.instanceId)
                }
            }
        }
    }
}
