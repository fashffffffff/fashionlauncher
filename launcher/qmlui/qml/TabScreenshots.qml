import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel

// Screenshots of the instance, newest first. Click one to view it large.
Item {
    id: tab
    property string instanceId
    property var instance
    readonly property string dir: instance ? instance.gameRoot + "/screenshots" : ""

    FolderListModel {
        id: shots
        folder: tab.dir ? "file://" + tab.dir : ""
        nameFilters: ["*.png", "*.jpg", "*.jpeg"]
        showDirs: false
        sortField: FolderListModel.Time
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(14)
        RowLayout {
            Layout.fillWidth: true
            Txt { text: qsTr("%n screenshot(s)", "", shots.count); muted: true; Layout.fillWidth: true }
            QuietButton { iconName: "folder"; text: qsTr("Open folder"); onClicked: Launcher.openScreenshotsFolder(tab.instanceId) }
        }
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: shots
            readonly property int columns: Math.max(1, Math.floor(width / Style.px(260)))
            cellWidth: width / columns
            cellHeight: cellWidth * 9 / 16 + Style.px(12)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                width: grid.cellWidth - Style.px(12)
                height: grid.cellHeight - Style.px(12)
                hoverEnabled: true
                Accessible.name: fileName
                onClicked: viewer.show(fileUrl)
                background: Rectangle { radius: Style.radius; color: Style.panel(1); clip: true }
                contentItem: Image {
                    source: fileUrl
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 520
                    layer.enabled: true
                }
            }
            Txt {
                anchors.centerIn: parent
                visible: shots.count === 0
                text: qsTr("No screenshots yet. Press F2 in game to take one.")
                muted: true
            }
        }
    }

    Popup {
        id: viewer
        property url src
        function show(u) { src = u; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent.width * 0.86
        height: parent.height * 0.86
        modal: true
        background: Rectangle { color: "transparent" }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.85) }
        contentItem: Item {
            Image { anchors.fill: parent; source: viewer.src; fillMode: Image.PreserveAspectFit; asynchronous: true }
            TapHandler { onTapped: viewer.close() }
            RowLayout {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Style.px(12) }
                spacing: Style.px(10)
                PrimaryButton {
                    iconName: "image"
                    text: qsTr("Use as this instance's background")
                    onClicked: {
                        const file = Backgrounds.importFile(viewer.src)
                        if (file) {
                            Backgrounds.setInstanceBackground(tab.instanceId, "image", file)
                            Router.toast(qsTr("Background set for %1").arg(tab.instance ? tab.instance.name : ""))
                        }
                        viewer.close()
                    }
                }
                QuietButton { text: qsTr("Close"); onClicked: viewer.close() }
            }
        }
    }
}
