import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One gallery item: preview, name, author and its install state. Click opens the details.
AbstractButton {
    id: card
    property var item: ({})
    readonly property string installedAs: Gallery.installed[item.id] || ""
    readonly property bool inUse: installedAs.length > 0 && (item.type === "theme" ? Theme.themeId === installedAs
                                                                                 : Style.bgCfg.image === installedAs)
    readonly property bool busy: Gallery.busyId === item.id
    signal install()

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: item.name
    implicitHeight: Style.px(240)

    background: Rectangle {
        radius: Style.radius
        color: card.hovered ? Style.panel(1) : Style.panel(0)
        border.width: card.visualFocus ? 2 : 1
        border.color: card.visualFocus ? Style.accent : Style.panelBorder
        Behavior on color { ColorAnimation { duration: Style.dur(120) } }
    }

    contentItem: ColumnLayout {
        spacing: Style.px(10)
        // preview, 16:9
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            Rectangle {
                id: frame
                anchors.fill: parent
                radius: Style.radius - 2
                color: Style.alpha(Style.text, 0.05)
                clip: true
                GalleryThemePreview {
                    visible: card.item.type === "theme" && !!card.item.colors
                    anchors.fill: parent
                    colors: card.item.colors || ({})
                    radius: parent.radius
                }
                Image {
                    visible: card.item.type !== "theme" || (!card.item.colors && status === Image.Ready)
                    anchors.fill: parent
                    source: card.item.preview || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 520
                    // gentle fade in once loaded
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Style.dur(180) } }
                }
                Icon {
                    visible: card.item.type !== "theme" && !card.item.preview
                    anchors.centerIn: parent
                    name: "image"; size: Style.px(28); color: Style.textMuted
                }
            }
            // installed / in use badge
            Rectangle {
                visible: card.installedAs.length > 0
                anchors { top: parent.top; right: parent.right; margins: Style.px(8) }
                height: Style.px(24)
                width: badgeRow.implicitWidth + Style.px(16)
                radius: height / 2
                color: card.inUse ? Style.accent : Style.alpha(Style.bg, 0.85)
                Row {
                    id: badgeRow
                    anchors.centerIn: parent
                    spacing: Style.px(4)
                    Icon { name: "check"; size: Style.px(13); color: card.inUse ? Style.onAccent : Style.success; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: card.inUse ? qsTr("In use") : qsTr("Installed")
                        color: card.inUse ? Style.onAccent : Style.text
                        font.family: Style.ui; font.pixelSize: Style.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Style.px(40)
            spacing: Style.px(8)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.px(2)
                Txt { text: card.item.name; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Txt {
                    Layout.fillWidth: true
                    muted: true
                    font.pixelSize: Style.fontSmall
                    text: {
                        const by = card.item.author ? qsTr("by %1").arg(card.item.author) : ""
                        const dl = card.item.downloads >= 0 ? (card.item.downloads === 1 ? qsTr("1 download") : qsTr("%n downloads", "", card.item.downloads)) : ""
                        return [by, dl].filter(s => s).join(" · ")
                    }
                }
            }
            QuietButton {
                visible: card.installedAs.length === 0
                iconName: card.busy ? "" : "plus"
                text: card.busy ? qsTr("Installing…") : ""
                tip: qsTr("Install")
                enabled: Gallery.busyId.length === 0
                onClicked: card.install()
            }
        }
    }
}
