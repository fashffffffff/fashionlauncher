import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Big preview and actions for one gallery item.
Popup {
    id: det
    property var item: ({})
    readonly property string installedAs: Gallery.installed[item.id] || ""
    readonly property bool isTheme: item.type === "theme"
    readonly property bool inUse: installedAs.length > 0 && (isTheme ? Theme.themeId === installedAs : Style.bgCfg.image === installedAs)
    readonly property bool busy: Gallery.busyId === item.id
    signal install()
    signal use()

    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: Math.min(parent.width - Style.px(80), Style.px(760))
    modal: true
    padding: Style.px(20)
    background: Rectangle { radius: Style.radius + Style.px(4); color: Style.surface; border.width: 1; border.color: Style.line }
    Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }

    contentItem: ColumnLayout {
        spacing: Style.px(14)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            radius: Style.radius
            color: Style.alpha(Style.text, 0.05)
            clip: true
            GalleryThemePreview {
                visible: det.isTheme && !!det.item.colors
                anchors.fill: parent
                radius: parent.radius
                colors: det.item.colors || ({})
            }
            Image {
                id: bigImage
                visible: !det.isTheme || (!det.item.colors && status === Image.Ready)
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                // for backgrounds show the full picture, not the small preview
                source: det.opened ? (det.isTheme ? (det.item.preview || "") : (det.item.video ? det.item.preview || "" : det.item.file)) : ""
                sourceSize.width: 1280
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: !det.isTheme && det.opened && bigImage.status === Image.Loading
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.px(4)
                Text {
                    text: det.item.name || ""
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Txt {
                    muted: true
                    Layout.fillWidth: true
                    text: {
                        const parts = [det.isTheme ? qsTr("Theme") : (det.item.video ? qsTr("Video background") : qsTr("Background"))]
                        if (det.item.author) parts.push(qsTr("by %1").arg(det.item.author))
                        if (det.item.downloads >= 0) parts.push((det.item.downloads === 1 ? qsTr("1 download") : qsTr("%n downloads", "", det.item.downloads)))
                        return parts.join(" · ")
                    }
                }
            }
            QuietButton { plain: true; iconName: "close"; tip: qsTr("Close"); onClicked: det.close() }
        }
        Txt {
            visible: text.length > 0
            text: det.item.description || ""
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            Layout.fillWidth: true
        }
        Flow {
            visible: (det.item.tags || []).length > 0
            Layout.fillWidth: true
            spacing: Style.px(6)
            Repeater {
                model: det.item.tags || []
                Rectangle {
                    height: Style.px(24); width: tagText.implicitWidth + Style.px(16); radius: height / 2
                    color: Style.alpha(Style.text, 0.07)
                    Txt { id: tagText; anchors.centerIn: parent; text: modelData; font.pixelSize: Style.fontSmall; muted: true }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(10)
            PrimaryButton {
                visible: det.installedAs.length === 0
                text: det.busy ? qsTr("Installing…") : qsTr("Install")
                iconName: det.busy ? "" : "plus"
                progress: det.busy ? -1 : undefined
                enabled: Gallery.busyId.length === 0
                onClicked: det.install()
            }
            PrimaryButton {
                visible: det.installedAs.length > 0 && !det.inUse
                text: det.isTheme ? qsTr("Apply") : qsTr("Use as background")
                iconName: "check"
                onClicked: det.use()
            }
            Txt {
                visible: det.inUse
                text: det.isTheme ? qsTr("This theme is in use.") : qsTr("This is your background now.")
                color: Style.success
            }
            Item { Layout.fillWidth: true }
            QuietButton {
                visible: det.installedAs.length > 0
                text: qsTr("Install again")
                iconName: "refresh"
                enabled: Gallery.busyId.length === 0
                onClicked: det.install()
            }
        }
    }
}
