import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// One instance: header with Play, then labeled tabs for everything you can do with it.
Item {
    id: page
    property var params: ({})
    readonly property string instanceId: params.instanceId || ""
    property var inst: null
    property string tab: params.tab || "overview"

    function refresh() {
        const i = Instances.indexOf(instanceId)
        inst = i >= 0 ? Instances.get(i) : null
        if (!inst && instanceId) Router.back()   // deleted
    }
    Component.onCompleted: { Router.selectedId = instanceId; refresh() }
    Connections {
        target: Instances
        function onDataChanged() { page.refresh() }
        function onCountChanged() { page.refresh() }
    }

    readonly property var tabs: [
        { id: "overview", label: qsTr("Overview"), source: "TabOverview.qml" },
        { id: "mods", label: qsTr("Mods"), source: "TabResources.qml", kind: "mods" },
        { id: "resourcepacks", label: qsTr("Resource packs"), source: "TabResources.qml", kind: "resourcepacks" },
        { id: "shaderpacks", label: qsTr("Shaders"), source: "TabResources.qml", kind: "shaderpacks" },
        { id: "worlds", label: qsTr("Worlds"), source: "TabWorlds.qml" },
        { id: "screenshots", label: qsTr("Screenshots"), source: "TabScreenshots.qml" },
        { id: "history", label: qsTr("History"), source: "TabHistory.qml" },
        { id: "settings", label: qsTr("Settings"), source: "TabInstanceSettings.qml" }
    ]
    readonly property var currentTab: tabs.find(t => t.id === tab) || tabs[0]

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(18)

        QuietButton {
            plain: true
            iconName: "arrow-left"
            text: qsTr("Back")
            onClicked: Router.back()
        }

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(18)
            Image {
                source: page.inst ? page.inst.icon : ""
                Layout.preferredWidth: Style.px(64); Layout.preferredHeight: Style.px(64)
                sourceSize: Qt.size(128, 128)
                smooth: false
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.px(4)
                Text {
                    Layout.fillWidth: true
                    text: page.inst ? page.inst.name : ""
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.px(30)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Txt {
                    text: [Fmt.versionLine(page.inst), page.inst ? Fmt.playTime(page.inst.playTime) : ""].filter(s => s).join("     ")
                    muted: true
                    font.pixelSize: Style.fontLead
                }
            }
            PlayButton { instanceId: page.instanceId; Layout.preferredWidth: Style.px(200) }
        }

        // Tabs
        Flickable {
            Layout.fillWidth: true
            implicitHeight: tabRow.implicitHeight
            contentWidth: tabRow.implicitWidth
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: tabRow
                spacing: Style.px(4)
                Repeater {
                    model: page.tabs
                    delegate: AbstractButton {
                        id: tb
                        readonly property bool on: page.currentTab.id === modelData.id
                        implicitHeight: Style.px(38)
                        implicitWidth: tbText.implicitWidth + Style.px(28)
                        hoverEnabled: true
                        Accessible.name: modelData.label
                        onClicked: page.tab = modelData.id
                        background: Rectangle {
                            radius: height / 2
                            color: tb.on ? Style.accent : tb.hovered ? Style.alpha(Style.text, 0.08) : Style.panel(0)
                            border.width: tb.visualFocus ? 2 : (tb.on ? 0 : 1)
                            border.color: tb.visualFocus ? Style.text : Style.panelBorder
                        }
                        contentItem: Text {
                            id: tbText
                            text: modelData.label
                            color: tb.on ? Style.onAccent : Style.text
                            font.family: Style.ui
                            font.pixelSize: Style.fontBody
                            font.weight: tb.on ? Font.DemiBold : Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        // Tab content
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radius + Style.px(4)
            color: Style.panel(0)
            border.width: 1
            border.color: Style.panelBorder
            Loader {
                anchors { fill: parent; margins: Style.px(20) }
                source: page.currentTab.source
                // Every tab gets the instance and, for resource tabs, which folder it manages.
                onLoaded: {
                    item.instanceId = Qt.binding(() => page.instanceId)
                    item.instance = Qt.binding(() => page.inst)
                    if (item.kind !== undefined) item.kind = Qt.binding(() => page.currentTab.kind || "")
                }
            }
        }
    }
}
