import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One installed mod / pack: icon, name, version, author, on/off switch, actions.
// Used as a delegate of InstalledModel (reads the model roles).
Rectangle {
    id: row
    signal toggled(bool on)
    signal removeRequested()
    signal homepageRequested()
    readonly property bool on: model.enabled

    implicitHeight: Style.px(64)
    radius: Style.radius
    color: hover.hovered ? Style.panel(1) : Style.alpha(Style.text, 0.03)
    border.width: 1
    border.color: Style.panelBorder

    HoverHandler { id: hover }
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: menu.popup() }

    RowLayout {
        anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(8) }
        spacing: Style.px(12)
        opacity: row.on ? 1 : 0.55

        Rectangle {
            Layout.preferredWidth: Style.px(40); Layout.preferredHeight: Style.px(40)
            radius: Style.radius - 4
            color: Style.alpha(Style.text, 0.06)
            Image {
                id: pic
                anchors.fill: parent
                source: model.icon
                sourceSize: Qt.size(80, 80)
                fillMode: Image.PreserveAspectFit
                smooth: false
                cache: false
                visible: status === Image.Ready
            }
            Icon {
                anchors.centerIn: parent
                visible: !pic.visible
                name: model.isFolder ? "folder" : "cube"
                size: Style.px(20)
                color: Style.textMuted
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.px(2)
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(8)
                Txt {
                    Layout.maximumWidth: row.width * 0.45
                    text: model.name
                    font.pixelSize: Style.fontLead
                    font.weight: Font.Medium
                }
                Txt { visible: !!model.version; text: model.version; muted: true }
                Rectangle {
                    visible: !row.on
                    implicitWidth: offText.implicitWidth + Style.px(14); implicitHeight: Style.px(20)
                    radius: height / 2
                    color: Style.alpha(Style.text, 0.10)
                    Txt { id: offText; anchors.centerIn: parent; text: qsTr("Turned off"); font.pixelSize: Style.fontSmall }
                }
                Rectangle {
                    visible: !!model.newVersion
                    implicitWidth: updText.implicitWidth + Style.px(14); implicitHeight: Style.px(20)
                    radius: height / 2
                    color: Style.alpha(Style.accent, 0.22)
                    Txt { id: updText; anchors.centerIn: parent; text: qsTr("Update: %1").arg(model.newVersion); font.pixelSize: Style.fontSmall }
                }
                Rectangle {
                    visible: !!model.issues
                    implicitWidth: issText.implicitWidth + Style.px(14); implicitHeight: Style.px(20)
                    radius: height / 2
                    color: Style.alpha(Style.danger, 0.22)
                    Txt { id: issText; anchors.centerIn: parent; text: qsTr("May not work"); font.pixelSize: Style.fontSmall }
                    HoverHandler { id: issHover }
                    ToolTip.visible: issHover.hovered
                    ToolTip.text: model.issues
                }
                Item { Layout.fillWidth: true }
            }
            Txt {
                Layout.fillWidth: true
                muted: true
                font.pixelSize: Style.fontSmall
                text: [model.author ? qsTr("by %1").arg(model.author) : "", model.provider, model.fileName].filter(s => s).join("  ·  ")
            }
        }

        ResSwitch {
            checked: row.on
            Accessible.name: row.on ? qsTr("Turn off %1").arg(model.name) : qsTr("Turn on %1").arg(model.name)
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: row.on ? qsTr("On — click to turn off") : qsTr("Off — click to turn on")
            onToggled: row.toggled(checked)
        }
        QuietButton {
            plain: true
            iconName: "trash"
            tip: qsTr("Remove")
            danger: true
            onClicked: row.removeRequested()
        }
        QuietButton {
            plain: true
            iconName: "more"
            tip: qsTr("More")
            onClicked: menu.popup()
        }
    }

    ThemedMenu {
        id: menu
        ThemedMenuItem { text: row.on ? qsTr("Turn off") : qsTr("Turn on"); iconName: "check"; onTriggered: row.toggled(!row.on) }
        ThemedMenuItem { text: qsTr("Open website"); iconName: "link"; enabled: !!model.homepage; onTriggered: row.homepageRequested() }
        ThemedMenuItem { text: qsTr("Remove"); iconName: "trash"; danger: true; onTriggered: row.removeRequested() }
    }
}
