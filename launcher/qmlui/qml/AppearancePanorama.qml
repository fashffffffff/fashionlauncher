import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Appearance → Background → Panorama: where the six pictures come from.
//   Automatic       the selected instance's resource packs, a panorama captured in it, or its Minecraft version
//   Your own        a resource pack .zip, six face pictures or one 360° picture (dropped here or picked)
//   Capture         installs a panorama-screenshot mod into the selected instance
ColumnLayout {
    id: pano
    spacing: Style.px(10)

    readonly property string source: Backgrounds.panoramaSource
    readonly property string instanceId: Router.selectedId
    readonly property var inst: { Instances.count; const i = Instances.indexOf(instanceId); return i >= 0 ? Instances.get(i) : null }
    readonly property var cap: Backgrounds.capture.instanceId === instanceId ? Backgrounds.capture : ({})
    property bool importing: false

    Connections {
        target: Backgrounds
        function onPanoramaImported(name, error) {
            pano.importing = false
            if (error) Router.toast(error, true)
            else Router.toast(qsTr("Panorama added."))
        }
    }
    function importUrls(urls) {
        importing = true
        Backgrounds.importPanorama(urls)
    }

    // Where the picture shown right now came from.
    Txt {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        muted: true
        text: Backgrounds.panorama.busy ? qsTr("Looking for a panorama…")
            : (Backgrounds.panorama.faces || []).length === 6 ? qsTr("Showing: %1").arg(Backgrounds.panorama.origin)
            : pano.source === "auto" && pano.inst ? qsTr("No panorama found for %1 yet. Start it once so Minecraft is downloaded, or add your own below.").arg(pano.inst.name)
            : qsTr("No panorama found yet. Start any instance once, or add your own below.")
    }

    Flow {
        id: grid
        Layout.fillWidth: true
        spacing: Style.px(10)
        readonly property int columns: pano.width > Style.px(420) ? 3 : 2
        readonly property real tileWidth: Math.floor((pano.width - (columns - 1) * spacing) / columns)

        BgThumb {
            width: grid.tileWidth
            height: Math.round(width * 9 / 16)
            url: pano.source === "auto" && (Backgrounds.panorama.faces || []).length === 6 ? Backgrounds.panorama.faces[0] : ""
            title: qsTr("Automatic")
            selected: pano.source === "auto"
            onClicked: Backgrounds.panoramaSource = "auto"
            Icon {
                visible: parent.url === ""
                anchors.centerIn: parent
                name: "cube"
                size: Style.px(28)
                color: Style.textMuted
            }
        }
        Repeater {
            model: Backgrounds.customPanoramas
            delegate: BgThumb {
                width: grid.tileWidth
                height: Math.round(width * 9 / 16)
                url: modelData.thumb
                title: modelData.name
                removable: true
                selected: pano.source === modelData.name
                onClicked: Backgrounds.panoramaSource = modelData.name
                onRemoveRequested: Backgrounds.removePanorama(modelData.name)
            }
        }
        // Drop zone / picker for your own.
        AbstractButton {
            id: add
            width: grid.tileWidth
            height: Math.round(width * 9 / 16)
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: qsTr("Add a panorama")
            onClicked: dialog.open()
            background: Rectangle {
                radius: Style.radius
                color: drop.containsDrag ? Style.alpha(Style.accent, 0.18) : add.hovered ? Style.alpha(Style.text, 0.08) : Style.alpha(Style.text, 0.03)
                border.width: add.visualFocus || drop.containsDrag ? 2 : 1
                border.color: add.visualFocus || drop.containsDrag ? Style.accent : Style.alpha(Style.text, 0.18)
            }
            contentItem: ColumnLayout {
                spacing: Style.px(6)
                Item { Layout.fillHeight: true }
                BusyIndicator { visible: pano.importing; running: visible; Layout.alignment: Qt.AlignHCenter; Layout.preferredHeight: Style.px(28); Layout.preferredWidth: Style.px(28) }
                Icon { visible: !pano.importing; name: "plus"; size: Style.px(22); Layout.alignment: Qt.AlignHCenter }
                Txt {
                    text: pano.importing ? qsTr("Making the cube…") : qsTr("Add or drop here")
                    Layout.alignment: Qt.AlignHCenter
                }
                Item { Layout.fillHeight: true }
            }
            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["text/uri-list"]
                onEntered: (d) => d.accepted = d.urls.length === 1 || d.urls.length === 6
                onDropped: (d) => pano.importUrls(d.urls)
            }
        }
    }
    Txt {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        muted: true
        font.pixelSize: Style.fontSmall
        text: qsTr("Works with a resource pack (.zip) that has a panorama, six pictures named panorama_0 to panorama_5, or one 360° picture twice as wide as it is tall.")
    }

    // Capture from my world
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Style.px(6)
        spacing: Style.px(8)
        Text {
            text: qsTr("Capture from my world")
            color: Style.text
            font.family: Style.ui
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
        }
        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            muted: true
            text: pano.inst
                ? qsTr("Adds a small panorama mod to %1. In the game, press one key and it takes six pictures around you; the next time you open the launcher with this instance selected, they turn into your panorama.").arg(pano.inst.name)
                : qsTr("Select an instance first, then come back here.")
        }
        RowLayout {
            spacing: Style.px(10)
            visible: pano.inst !== null
            QuietButton {
                text: pano.cap.state === "done" ? qsTr("Installed") : qsTr("Install panorama mod")
                iconName: pano.cap.state === "done" ? "check" : "image"
                enabled: pano.cap.state !== "working" && pano.cap.state !== "done"
                onClicked: Backgrounds.installPanoramaMod(pano.instanceId)
            }
            BusyIndicator { visible: pano.cap.state === "working"; running: visible; Layout.preferredHeight: Style.px(24); Layout.preferredWidth: Style.px(24) }
        }
        Txt {
            Layout.fillWidth: true
            visible: (pano.cap.message || "") !== ""
            wrapMode: Text.WordWrap
            text: pano.cap.message || ""
            color: pano.cap.state === "error" ? Style.danger : Style.text
        }
    }

    FileDialog {
        id: dialog
        title: qsTr("Choose a panorama")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Panoramas (*.zip *.png *.jpg *.jpeg *.webp)")]
        onAccepted: pano.importUrls(selectedFiles)
    }
}
