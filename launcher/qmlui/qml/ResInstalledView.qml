import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FFPrism.Resources
import "ResFmt.js" as RF

// What is installed in one folder of an instance (mods, resource packs or shaders).
Item {
    id: view
    property string instanceId
    property string kind: "mods"
    signal findRequested()

    InstalledModel {
        id: installed
        instanceId: view.instanceId
        kind: view.kind
        filter: search.text
        onMessage: (text, isError) => Router.toast(text, isError)
    }

    ResUpdatesDialog { id: updatesDialog; model: installed }

    // Opened from the pre-launch check ("Install Fabric API"): go straight to Find
    Component.onCompleted: if (view.kind === "mods" && Tools.hasPendingFind(view.instanceId)) Qt.callLater(view.findRequested)

    FileDialog {
        id: fileDialog
        title: qsTr("Add files")
        fileMode: FileDialog.OpenFiles
        nameFilters: view.kind === "mods" ? [qsTr("Mods (*.jar *.zip *.litemod)"), qsTr("All files (*)")]
                                          : [qsTr("Packs (*.zip)"), qsTr("All files (*)")]
        onAccepted: installed.addFiles(selectedFiles)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(14)

        // Mods without a loader would never load
        Rectangle {
            visible: view.kind === "mods" && view.instanceId.length > 0 && !installed.hasModLoader
            Layout.fillWidth: true
            implicitHeight: noLoader.implicitHeight + Style.px(24)
            radius: Style.radius
            color: Style.alpha(Style.danger, 0.12)
            border.width: 1
            border.color: Style.alpha(Style.danger, 0.4)
            RowLayout {
                id: noLoader
                anchors { fill: parent; margins: Style.px(12) }
                spacing: Style.px(12)
                Txt {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("This instance has no mod loader, so mods won't run. Install Fabric, Quilt, Forge or NeoForge in the instance's version settings.")
                }
                QuietButton { text: qsTr("Change version"); onClicked: Router.open("version", { instanceId: view.instanceId }) }
            }
        }

        // Toolbar
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Field {
                id: search
                iconName: "search"
                placeholderText: RF.searchPlaceholder(view.kind)
                Layout.fillWidth: true
                Layout.maximumWidth: Style.px(360)
                Keys.onEscapePressed: text = ""
            }
            Item { Layout.fillWidth: true }
            QuietButton { text: qsTr("Add files"); iconName: "plus"; onClicked: fileDialog.open() }
            QuietButton { text: qsTr("Open folder"); iconName: "folder"; onClicked: installed.openFolder() }
            QuietButton {
                visible: installed.totalCount > 0
                enabled: installed.busy === ""
                text: installed.busy === "checking" ? qsTr("Checking…") : qsTr("Check for updates")
                iconName: "refresh"
                onClicked: installed.checkUpdates()
            }
            PrimaryButton {
                visible: installed.updateCount > 0 || installed.busy === "updating"
                implicitHeight: Style.px(40)
                implicitWidth: Style.px(150)
                text: installed.busy === "updating" ? qsTr("Updating…") : qsTr("Update all (%1)").arg(installed.updateCount)
                iconName: "export"
                enabled: installed.busy === ""
                progress: installed.busy === "updating" ? installed.progress : undefined
                onClicked: updatesDialog.show()   // review versions and changelogs first
            }
            PrimaryButton {
                visible: installed.totalCount > 0
                implicitHeight: Style.px(40)
                implicitWidth: Math.max(Style.px(130), implicitContentWidth + Style.px(40))
                text: RF.findLabel(view.kind)
                iconName: "search"
                onClicked: view.findRequested()
            }
        }

        // Progress line for checks and updates
        RowLayout {
            visible: installed.busy !== ""
            Layout.fillWidth: true
            spacing: Style.px(10)
            Txt {
                Layout.fillWidth: true
                muted: true
                text: installed.statusText || (installed.busy === "checking" ? qsTr("Checking for updates…") : qsTr("Updating…"))
            }
            QuietButton { plain: true; text: qsTr("Cancel"); onClicked: installed.cancel() }
        }
        RowLayout {
            visible: installed.busy === "" && installed.checked && installed.updateCount > 0
            spacing: Style.px(8)
            Txt { muted: true; text: qsTr("%n update(s) available", "", installed.updateCount) }
            QuietButton { plain: true; text: qsTr("See what's new"); onClicked: updatesDialog.show() }
        }

        // Pre-launch checks were turned off from the launch dialog: offer them back here
        RowLayout {
            visible: view.kind === "mods" && !Tools.checksEnabled
            spacing: Style.px(8)
            Txt { muted: true; text: qsTr("Mods aren't checked before playing.") }
            QuietButton { plain: true; text: qsTr("Turn checks on"); onClicked: Tools.checksEnabled = true }
        }

        // The list
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: installed
                spacing: Style.px(6)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ResInstalledRow {
                    width: ListView.view.width
                    onToggled: (on) => installed.setItemEnabled(index, on)
                    onRemoveRequested: installed.remove(index)
                    onHomepageRequested: installed.openHomepage(index)
                }
            }

            // Nothing installed
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - Style.px(40), Style.px(440))
                visible: !installed.loading && installed.totalCount === 0
                spacing: Style.px(12)
                Icon { Layout.alignment: Qt.AlignHCenter; name: view.kind === "mods" ? "cube" : view.kind === "shaderpacks" ? "globe" : "image"; size: Style.px(40); color: Style.textMuted }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: RF.emptyTitle(view.kind)
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                }
                Txt {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    muted: true
                    text: RF.emptyBody(view.kind)
                }
                PrimaryButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Style.px(6)
                    text: RF.findLabel(view.kind)
                    iconName: "search"
                    onClicked: view.findRequested()
                }
            }
            Txt {
                anchors.centerIn: parent
                visible: installed.totalCount > 0 && list.count === 0
                muted: true
                font.pixelSize: Style.fontLead
                text: qsTr("Nothing matches “%1”").arg(search.text)
            }
            Txt {
                anchors.centerIn: parent
                visible: installed.loading && installed.totalCount === 0
                muted: true
                text: qsTr("Loading…")
            }
        }
    }

    // Drag files in from Finder/Explorer
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (d) => { if (d.hasUrls) { installed.addFiles(d.urls); d.acceptProposedAction() } }
    }
    Rectangle {
        anchors.fill: parent
        visible: drop.containsDrag
        radius: Style.radius
        color: Style.alpha(Style.accent, 0.14)
        border.width: 2
        border.color: Style.accent
        Txt {
            anchors.centerIn: parent
            text: qsTr("Drop to add to this instance")
            font.pixelSize: Style.fontLead
            font.weight: Font.DemiBold
        }
    }
}
