import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import FFPrism
import "Fmt.js" as Fmt

// All instances. Click a card to open it; the play button on the card launches right away.
Item {
    id: page
    property var params: ({})
    InstancesView { id: view; filter: search.text; sortMode: Settings.get("QmlInstanceSort") || "recent" }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(20)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            Text {
                text: qsTr("Instances")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(32)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Field {
                id: search
                iconName: "search"
                placeholderText: qsTr("Search instances")
                Layout.preferredWidth: Style.px(280)
                Keys.onEscapePressed: text = ""
            }
            PrimaryButton {
                text: qsTr("Create instance")
                iconName: "plus"
                implicitHeight: Style.px(40)
                onClicked: Router.open("create")
            }
        }

        // Sort and group filter
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(10)
            Txt { text: qsTr("Sort"); muted: true }
            SettingsSegmented {
                value: view.sortMode
                options: [{ value: "recent", label: qsTr("Recently played") }, { value: "name", label: qsTr("Name") },
                          { value: "playtime", label: qsTr("Most played") }]
                onPicked: (v) => { view.sortMode = v; Settings.set("QmlInstanceSort", v) }
            }
            Item { Layout.preferredWidth: Style.px(12) }
            Txt { visible: view.groups.length > 0; text: qsTr("Group"); muted: true }
            SettingsDropdown {
                visible: view.groups.length > 0
                implicitWidth: Style.px(200)
                value: view.group
                options: [{ value: "", label: qsTr("All groups") }, { value: "-", label: qsTr("Without a group") }]
                         .concat(view.groups.map(g => ({ value: g, label: g })))
                onPicked: (v) => view.group = v
            }
            Item { Layout.fillWidth: true }
        }

        // Instances that are still being created/imported: a placeholder card with live progress.
        Flow {
            id: pending
            readonly property var items: Tasks.revision >= 0 ? Tasks.ofKind("create") : []
            Layout.fillWidth: true
            visible: items.length > 0
            spacing: Style.px(12)
            Repeater {
                model: pending.items
                delegate: Rectangle {
                    required property var modelData
                    width: grid.cellWidth - Style.px(12)
                    height: grid.cellHeight - Style.px(12)
                    radius: Style.radius
                    color: Style.panel(0)
                    border.width: 1
                    border.color: Style.accent
                    RowLayout {
                        anchors { fill: parent; leftMargin: Style.px(14); rightMargin: Style.px(14) }
                        spacing: Style.px(14)
                        BusyIndicator { Layout.preferredWidth: Style.px(48); Layout.preferredHeight: Style.px(48); running: true }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(4)
                            Txt { text: modelData.title; Layout.fillWidth: true; font.pixelSize: Style.fontLead; font.weight: Font.Medium }
                            Txt {
                                Layout.fillWidth: true
                                muted: true
                                text: (modelData.progress >= 0 ? Math.round(modelData.progress * 100) + "% · " : "")
                                      + (modelData.status || qsTr("Installing…"))
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: modelData.progress >= 0 ? modelData.progress : 0
                                indeterminate: modelData.progress < 0
                            }
                        }
                        QuietButton {
                            iconName: "close"; plain: true; tip: qsTr("Cancel")
                            visible: modelData.canCancel
                            onClicked: Tasks.cancel(modelData.row)
                        }
                    }
                }
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view
            readonly property int columns: Math.max(1, Math.floor(width / Style.px(280)))
            cellWidth: width / columns
            readonly property bool covers: Backgrounds.coversEnabled
            readonly property real coverHeight: covers ? Style.px(78) : 0
            cellHeight: Style.px(112) + coverHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight
                AbstractButton {
                    id: tile
                    anchors { fill: parent; rightMargin: Style.px(12); bottomMargin: Style.px(12) }
                    hoverEnabled: true
                    Accessible.name: model.name
                    onClicked: Router.openInstance(model.instanceId)
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: tileMenu.popup() }

                    readonly property string cover: grid.covers ? (Backgrounds.covers[model.instanceId] || model.backdrop || "") : ""
                    Component.onCompleted: if (grid.covers) Backgrounds.requestCover(model.instanceId)

                    background: Rectangle {
                        radius: Style.radius
                        color: tile.hovered ? Style.panel(1) : Style.panel(0)
                        border.width: tile.visualFocus ? 2 : 1
                        border.color: tile.visualFocus ? Style.accent : Style.panelBorder

                        // Cover strip: the pack's picture or the newest screenshot, cropped and darkened a little.
                        // Pictures are decoded at card size; the strip is clipped to the card's rounded top.
                        Item {
                            id: strip
                            visible: grid.covers
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
                            height: grid.coverHeight
                            layer.enabled: visible
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: stripMask
                                maskThresholdMin: 0.5
                                maskSpreadAtMin: 1.0
                            }
                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: Style.mix(Style.surfaceHi, Style.accent, 0.35) }
                                    GradientStop { position: 1; color: Style.mix(Style.surface, Style.accent, 0.12) }
                                }
                            }
                            Image {
                                id: coverImg
                                anchors.fill: parent
                                source: tile.cover
                                sourceSize.width: 640
                                fillMode: Image.PreserveAspectCrop
                                verticalAlignment: Image.AlignVCenter
                                asynchronous: true
                                opacity: status === Image.Ready ? 1 : 0
                                scale: tile.hovered ? 1.04 : 1
                                Behavior on opacity { NumberAnimation { duration: Style.dur(250) } }
                                Behavior on scale { NumberAnimation { duration: Style.dur(300); easing.type: Easing.OutCubic } }
                            }
                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0; color: Style.alpha(Style.scrim, 0.08) }
                                    GradientStop { position: 1; color: Style.alpha(Style.scrim, 0.45) }
                                }
                            }
                        }
                        Item {
                            id: stripMask
                            anchors.fill: strip
                            visible: false
                            layer.enabled: grid.covers
                            // Rounded on top, square at the bottom.
                            Rectangle { anchors.fill: parent; anchors.bottomMargin: -Style.radius; radius: Style.radius - 1 }
                        }
                    }
                    contentItem: RowLayout {
                        spacing: Style.px(14)
                        anchors { fill: parent; topMargin: grid.coverHeight; leftMargin: Style.px(14); rightMargin: Style.px(14) }
                        Image {
                            source: model.icon
                            Layout.preferredWidth: Style.px(48)
                            Layout.preferredHeight: Style.px(48)
                            sourceSize: Qt.size(96, 96)
                            smooth: false
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(4)
                            Txt { text: model.name; Layout.fillWidth: true; font.pixelSize: Style.fontLead; font.weight: Font.Medium }
                            Txt {
                                readonly property real job: Tasks.revision >= 0 ? Tasks.instanceProgress(model.instanceId) : -2
                                text: job > -2 ? (job >= 0 ? Math.round(job * 100) + "% · " : "") + Tasks.instanceStatus(model.instanceId)
                                               : (Fmt.versionLine(model) || Fmt.playTime(model.playTime))
                                color: job > -2 ? Style.accent : Style.textMuted
                                Layout.fillWidth: true
                            }
                        }
                        // Launch without opening the instance
                        AbstractButton {
                            id: quickPlay
                            readonly property var st: Launcher.launches[model.instanceId]
                            Layout.preferredWidth: Style.px(40)
                            Layout.preferredHeight: Style.px(40)
                            hoverEnabled: true
                            Accessible.name: st ? qsTr("Stop") : qsTr("Play")
                            ToolTip.visible: hovered
                            ToolTip.text: st ? (st.phase === "running" ? qsTr("Stop") : st.status) : qsTr("Play")
                            onClicked: st ? Launcher.kill(model.instanceId) : Launcher.launch(model.instanceId)
                            background: Rectangle {
                                radius: width / 2
                                color: quickPlay.st ? Style.success : quickPlay.hovered ? Style.accent : Style.alpha(Style.accent, 0.85)
                            }
                            contentItem: Item {
                                Icon { anchors.centerIn: parent; name: quickPlay.st ? "stop" : "play"; size: Style.px(16); color: Style.onAccent }
                            }
                        }
                    }
                    ThemedMenu {
                        id: tileMenu
                        ThemedMenuItem { text: qsTr("Play"); iconName: "play"; onTriggered: Launcher.launch(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Open"); iconName: "sliders"; onTriggered: Router.openInstance(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Open folder"); iconName: "folder"; onTriggered: Launcher.openInstanceFolder(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Copy instance"); iconName: "copy"; onTriggered: Launcher.copyInstance(model.instanceId) }
                        ThemedMenuItem { text: qsTr("Delete"); iconName: "trash"; danger: true; onTriggered: Launcher.deleteInstance(model.instanceId) }
                    }
                }
            }

            Txt {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: search.text.length > 0 ? qsTr("No instances match “%1”").arg(search.text) : qsTr("No instances yet. Create one to start playing.")
                muted: true
                font.pixelSize: Style.fontLead
            }
        }
    }
}
