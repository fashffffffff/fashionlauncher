import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ResFmt.js" as RF

// Details of the selected search result: description, version picker and Install.
Rectangle {
    id: panel
    property var search            // SearchModel
    readonly property var info: search ? search.selected : ({})
    property int versionIndex: 0
    property var lastRow: -1
    // A different project starts at its newest version
    onInfoChanged: if (info.row !== lastRow) { lastRow = info.row; versionIndex = 0 }

    radius: Style.radius
    color: Style.panel(1)
    border.width: 1
    border.color: Style.panelBorder

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(16) }
        spacing: Style.px(12)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            Rectangle {
                Layout.preferredWidth: Style.px(64); Layout.preferredHeight: Style.px(64)
                radius: Style.radius - 2
                color: Style.alpha(Style.text, 0.06)
                clip: true
                Image {
                    id: logo
                    anchors.fill: parent
                    source: panel.info.iconUrl || ""
                    sourceSize: Qt.size(128, 128)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: status === Image.Ready
                }
                Icon { anchors.centerIn: parent; visible: !logo.visible; name: "cube"; size: Style.px(26); color: Style.textMuted }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.px(2)
                Text {
                    Layout.fillWidth: true
                    text: panel.info.title || ""
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Txt { Layout.fillWidth: true; visible: !!panel.info.author; text: qsTr("by %1").arg(panel.info.author); muted: true }
                Txt {
                    visible: panel.info.downloads >= 0
                    text: qsTr("%1 downloads").arg(RF.count(panel.info.downloads))
                    muted: true
                    font.pixelSize: Style.fontSmall
                }
            }
            QuietButton {
                Layout.alignment: Qt.AlignTop
                plain: true
                iconName: "close"
                tip: qsTr("Close details")
                onClicked: panel.search.selectedRow = -1
            }
        }

        Txt {
            Layout.fillWidth: true
            text: panel.info.summary || ""
            wrapMode: Text.WordWrap
            maximumLineCount: 6
        }
        QuietButton {
            visible: !!panel.info.url
            text: qsTr("Open website")
            iconName: "link"
            onClicked: panel.search.openWebsite(panel.info.row)
        }

        Text {
            text: qsTr("Version")
            color: Style.text
            font.family: Style.ui
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            Layout.topMargin: Style.px(4)
        }

        // Version picker
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radius - 2
            color: Style.alpha(Style.bg, 0.25)
            border.width: 1
            border.color: Style.panelBorder
            ListView {
                id: versions
                anchors { fill: parent; margins: Style.px(4) }
                clip: true
                model: panel.info.versions || []
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ItemDelegate {
                    id: ver
                    width: ListView.view.width
                    implicitHeight: Style.px(50)
                    readonly property bool chosen: index === panel.versionIndex
                    onClicked: panel.versionIndex = index
                    Accessible.role: Accessible.RadioButton
                    Accessible.checked: chosen
                    background: Rectangle {
                        radius: Style.radius - 4
                        color: ver.chosen ? Style.alpha(Style.accent, 0.18) : ver.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                        border.width: ver.visualFocus ? 2 : 0
                        border.color: Style.accent
                    }
                    contentItem: RowLayout {
                        spacing: Style.px(10)
                        Rectangle {
                            Layout.preferredWidth: Style.px(14); Layout.preferredHeight: Style.px(14)
                            radius: width / 2
                            color: "transparent"
                            border.width: 2
                            border.color: ver.chosen ? Style.accent : Style.line
                            Rectangle { anchors.centerIn: parent; width: Style.px(6); height: width; radius: width / 2; color: Style.accent; visible: ver.chosen }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Txt { Layout.fillWidth: true; text: modelData.name; font.weight: Font.Medium }
                            Txt {
                                Layout.fillWidth: true
                                muted: true
                                font.pixelSize: Style.fontSmall
                                text: [modelData.mc, modelData.loaders, modelData.date].filter(s => s).join("  ·  ")
                            }
                        }
                        Rectangle {
                            visible: !!modelData.type && modelData.type !== "Release"
                            implicitWidth: vt.implicitWidth + Style.px(12); implicitHeight: Style.px(20)
                            radius: height / 2
                            color: Style.alpha(Style.text, 0.10)
                            Txt {
                                id: vt
                                anchors.centerIn: parent
                                font.pixelSize: Style.fontSmall
                                text: modelData.type === "Beta" ? qsTr("Beta") : modelData.type === "Alpha" ? qsTr("Alpha") : modelData.type
                            }
                        }
                    }
                }
            }
            Txt {
                anchors.centerIn: parent
                width: parent.width - Style.px(24)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                muted: true
                visible: versions.count === 0
                text: panel.info.versionsLoading ? qsTr("Loading versions…")
                    : panel.info.versionsError ? qsTr("Couldn't load versions. Check your connection.")
                    : panel.info.versionsLoaded ? qsTr("No version works with this instance.")
                    : ""
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(10)
            Row {
                visible: panel.info.state === "installed"
                spacing: Style.px(6)
                Icon { name: "check"; size: Style.px(18); color: Style.success; anchors.verticalCenter: parent.verticalCenter }
                Txt { text: qsTr("Installed"); font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            }
            PrimaryButton {
                Layout.fillWidth: true
                enabled: panel.info.state !== "installing" && panel.info.state !== "queued" && versions.count > 0
                text: panel.info.state === "installing" ? qsTr("Installing…")
                    : panel.info.state === "installed" ? qsTr("Install this version")
                    : qsTr("Install")
                iconName: panel.info.state === "installing" ? "" : "plus"
                progress: panel.info.state === "installing" ? panel.info.progress : undefined
                onClicked: panel.search.install(panel.info.row, panel.versionIndex)
            }
        }
    }
}
