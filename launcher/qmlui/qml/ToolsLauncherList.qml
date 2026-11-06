import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Instances found in other launchers on this computer, with checkboxes (used by CreateImport).
Item {
    id: root
    property var picked: ({})
    property bool linkFiles: false
    signal toggled(string key, bool on)
    signal linkToggled(bool on)
    signal chooseFolder()

    readonly property int importableCount: Tools.launchers.filter(e => e.importable).length

    function loaderLabel(e) {
        const names = { fabric: "Fabric", quilt: "Quilt", forge: "Forge", neoforge: "NeoForge" }
        const mc = e.mc ? qsTr("Minecraft %1").arg(e.mc) : ""
        return e.launcher + "  ·  " + (e.loader ? mc + "  ·  " + names[e.loader] + " " + e.loaderVersion : mc)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(10)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Txt {
                Layout.fillWidth: true
                muted: true
                elide: Text.ElideMiddle
                text: Tools.scanning ? qsTr("Looking…")
                    : Tools.scannedFolder ? qsTr("In %1").arg(Tools.scannedFolder)
                    : qsTr("%n instance(s) found", "", Tools.launchers.length)
            }
            QuietButton { text: qsTr("Look again"); iconName: "refresh"; enabled: !Tools.scanning; onClicked: Tools.refreshLaunchers() }
            QuietButton { text: qsTr("Choose folder…"); iconName: "folder"; enabled: !Tools.scanning; onClicked: root.chooseFolder() }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: Style.px(6)
                model: Tools.launchers
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: rowCard
                    required property var modelData
                    readonly property bool on: !!root.picked[modelData.key]
                    width: ListView.view.width - Style.px(10)
                    height: Style.px(56)
                    radius: Style.radius
                    opacity: modelData.importable ? 1 : 0.55
                    color: on ? Style.alpha(Style.accent, 0.12) : hover.hovered && modelData.importable ? Style.panel(1) : Style.panel(0)
                    border.width: 1
                    border.color: on ? Style.alpha(Style.accent, 0.6) : Style.panelBorder
                    HoverHandler { id: hover }
                    TapHandler { enabled: rowCard.modelData.importable && !Creator.busy; onTapped: root.toggled(rowCard.modelData.key, !rowCard.on) }
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: modelData.name
                    Accessible.checked: on

                    RowLayout {
                        anchors { fill: parent; leftMargin: Style.px(14); rightMargin: Style.px(14) }
                        spacing: Style.px(12)
                        Rectangle {
                            width: Style.px(20); height: width; radius: Style.px(5)
                            color: rowCard.on ? Style.accent : "transparent"
                            border.width: rowCard.on ? 0 : 1.5
                            border.color: Style.textMuted
                            Icon { anchors.centerIn: parent; visible: rowCard.on; name: "check"; size: Style.px(14); color: Style.onAccent }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(2)
                            Txt { Layout.fillWidth: true; text: rowCard.modelData.name; font.weight: Font.Medium; elide: Text.ElideRight }
                            Txt {
                                Layout.fillWidth: true
                                muted: true
                                font.pixelSize: Style.fontSmall
                                elide: Text.ElideRight
                                text: rowCard.modelData.importable ? root.loaderLabel(rowCard.modelData) : rowCard.modelData.launcher + "  ·  " + rowCard.modelData.note
                            }
                        }
                    }
                }
            }

            // Nothing found
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - Style.px(40), Style.px(420))
                visible: !Tools.scanning && Tools.launchers.length === 0
                spacing: Style.px(10)
                Icon { Layout.alignment: Qt.AlignHCenter; name: "search"; size: Style.px(36); color: Style.textMuted }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Nothing found")
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
                    text: qsTr("No other launchers in their usual places. If yours keeps its files somewhere else, show us the folder.")
                }
                QuietButton { Layout.alignment: Qt.AlignHCenter; text: qsTr("Choose folder…"); iconName: "folder"; onClicked: root.chooseFolder() }
            }
            Txt { anchors.centerIn: parent; visible: Tools.scanning && Tools.launchers.length === 0; muted: true; text: qsTr("Looking for other launchers…") }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.importableCount > 0
            spacing: Style.px(10)
            ResSwitch {
                id: linkSwitch
                checked: root.linkFiles
                enabled: !Creator.busy
                Accessible.name: qsTr("Share files instead of copying")
                onToggled: root.linkToggled(checked)
            }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSmall
                muted: !root.linkFiles
                text: root.linkFiles ? qsTr("Worlds, mods and settings stay shared with the other launcher: changes show up in both.")
                                     : qsTr("Share files instead of copying (saves space, both launchers see the same worlds)")
                TapHandler { onTapped: if (!Creator.busy) root.linkToggled(!root.linkFiles) }
            }
        }
    }
}
