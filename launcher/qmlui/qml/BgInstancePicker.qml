import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// "Background for this instance": a wallpaper, picture or video that replaces the theme background
// while this instance is selected, or "Follow theme". Embeddable anywhere: set instanceId.
ColumnLayout {
    id: picker
    property string instanceId
    readonly property var current: Backgrounds.overrides[instanceId] || null
    spacing: Style.px(10)

    function pick(kind, value) { Backgrounds.setInstanceBackground(instanceId, kind, value) }
    function isCurrent(kind, value) { return current !== null && current.kind === kind && current.value === value }

    Flow {
        Layout.fillWidth: true
        spacing: Style.px(8)
        QuietButton {
            text: qsTr("Follow theme")
            iconName: picker.current === null ? "check" : "palette"
            highlighted: picker.current === null
            onClicked: Backgrounds.clearInstanceBackground(picker.instanceId)
        }
        QuietButton { text: qsTr("Add image…"); iconName: "image"; onClicked: imageDialog.open() }
        QuietButton { text: qsTr("Add video…"); iconName: "play"; onClicked: videoDialog.open() }
    }

    ListView {
        id: strip
        Layout.fillWidth: true
        Layout.preferredHeight: Style.px(84)
        orientation: ListView.Horizontal
        spacing: Style.px(8)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        model: Backgrounds.userMedia.map(m => ({ kind: m.kind, value: m.file, url: m.url, title: m.file }))
               .concat(Backgrounds.wallpapers.map(w => ({ kind: "wallpaper", value: w.id, url: w.url, title: w.title })))
        delegate: BgThumb {
            width: Style.px(136)
            height: Style.px(76)
            url: modelData.url
            title: modelData.title
            video: modelData.kind === "video"
            selected: picker.isCurrent(modelData.kind, modelData.value)
            onClicked: picker.pick(modelData.kind, modelData.value)
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Choose a background image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: { const f = Backgrounds.importFile(selectedFile); if (f) picker.pick("image", f) }
    }
    FileDialog {
        id: videoDialog
        title: qsTr("Choose a background video")
        nameFilters: [qsTr("Videos (*.mp4 *.webm *.mov *.m4v)")]
        onAccepted: { const f = Backgrounds.importFile(selectedFile); if (f) picker.pick("video", f) }
    }
}
