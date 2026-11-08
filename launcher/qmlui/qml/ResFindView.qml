import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FFPrism.Resources
import "ResFmt.js" as RF

// Search Modrinth / CurseForge and install into one instance.
Item {
    id: view
    property string instanceId
    property string kind: "mods"
    readonly property alias search: results

    SearchModel {
        id: results
        instanceId: view.instanceId
        kind: view.kind
        query: queryField.text
        onMessage: (text, isError) => Router.toast(text, isError)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(12)

        // Search and filters
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Field {
                id: queryField
                iconName: "search"
                placeholderText: view.kind === "resourcepacks" ? qsTr("Search resource packs")
                               : view.kind === "shaderpacks" ? qsTr("Search shaders")
                               : qsTr("Search mods")
                Layout.fillWidth: true
                Keys.onEscapePressed: text = ""
                Keys.onReturnPressed: results.search()
                Component.onCompleted: {
                    if (Tools.hasPendingFind(view.instanceId)) text = Tools.takePendingFind(view.instanceId)
                    forceActiveFocus()
                }
            }
            ResSegmented {
                options: [{ value: "modrinth", label: "Modrinth" }, { value: "curseforge", label: "CurseForge" }]
                value: results.platform
                onPicked: (v) => results.platform = v
                Accessible.name: qsTr("Where to search")
            }
        }
        // Filters: wrap onto a second line in narrow windows instead of squeezing
        Flow {
            Layout.fillWidth: true
            spacing: Style.px(8)
            ResPicker {
                label: qsTr("Category")
                options: [{ value: "", label: qsTr("All") }].concat(results.categories.map(c => ({ value: c.id, label: c.name })))
                value: results.category
                onPicked: (v) => results.category = v
            }
            ResPicker {
                label: qsTr("Sort")
                options: results.sortOptions.map((o, i) => ({ value: String(i), label: o.label.replace(/^Sort by /, "") }))
                value: String(results.sortIndex)
                onPicked: (v) => results.sortIndex = parseInt(v)
            }
            ResPicker {
                // CurseForge's API has no client/server filter
                visible: view.kind === "mods" && results.platform === "modrinth"
                label: qsTr("Runs on")
                options: [{ value: "", label: qsTr("Any side") }, { value: "client", label: qsTr("Client") },
                          { value: "server", label: qsTr("Server") }]
                value: results.side
                onPicked: (v) => results.side = v
            }
            ResPicker {
                label: qsTr("Minecraft")
                options: [{ value: "", label: results.gameVersion ? qsTr("%1 (this instance)").arg(results.gameVersion) : qsTr("This instance") },
                          { value: "any", label: qsTr("Any version") }]
                         .concat(results.mcVersionChoices.filter(v => v !== results.gameVersion).map(v => ({ value: v, label: v })))
                value: results.mcFilter
                onPicked: (v) => results.mcFilter = v
            }
            ResPicker {
                visible: view.kind === "mods"
                label: qsTr("Loader")
                options: [{ value: "", label: results.loaderText ? qsTr("%1 (this instance)").arg(results.loaderText) : qsTr("This instance") },
                          { value: "any", label: qsTr("Any loader") },
                          { value: "fabric", label: "Fabric" }, { value: "quilt", label: "Quilt" },
                          { value: "forge", label: "Forge" }, { value: "neoforge", label: "NeoForge" }]
                value: results.loaderFilter
                onPicked: (v) => results.loaderFilter = v
            }
        }
        // Browsing outside the instance's version or loader: say what can still be installed
        Txt {
            Layout.fillWidth: true
            visible: view.kind === "mods" && (results.mcFilter !== "" || results.loaderFilter !== "") && results.gameVersion.length > 0
            muted: true
            wrapMode: Text.WordWrap
            text: results.loaderText ? qsTr("Showing more than fits this instance. A mod installs only if it also has a version for Minecraft %1 with %2.").arg(results.gameVersion).arg(results.loaderText)
                                     : qsTr("Showing more than fits this instance. A mod installs only if it also has a version for Minecraft %1.").arg(results.gameVersion)
        }

        Rectangle {
            visible: view.kind === "mods" && !results.hasModLoader && view.instanceId.length > 0
            Layout.fillWidth: true
            implicitHeight: warn.implicitHeight + Style.px(20)
            radius: Style.radius
            color: Style.alpha(Style.danger, 0.12)
            border.width: 1
            border.color: Style.alpha(Style.danger, 0.4)
            Txt {
                id: warn
                anchors { fill: parent; margins: Style.px(10) }
                wrapMode: Text.WordWrap
                text: qsTr("This instance has no mod loader, so mods won't run. Install Fabric, Quilt, Forge or NeoForge in the instance's version settings.")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(14)

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    model: results
                    spacing: Style.px(8)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    onAtYEndChanged: if (atYEnd && count > 0) results.loadMore()
                    delegate: ResCard {
                        width: ListView.view.width - Style.px(10)
                        current: results.selectedRow === index
                        onClicked: results.selectedRow = (results.selectedRow === index ? -1 : index)
                        onPickToggled: results.togglePick(index)
                    }
                    footer: Item {
                        width: ListView.view.width
                        height: results.canLoadMore || (results.busy && list.count > 0) ? Style.px(56) : 0
                        QuietButton {
                            anchors.centerIn: parent
                            visible: results.canLoadMore || results.busy
                            enabled: !results.busy
                            text: results.busy ? qsTr("Loading…") : qsTr("Show more")
                            onClicked: results.loadMore()
                        }
                    }
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - Style.px(40), Style.px(420))
                    visible: list.count === 0
                    spacing: Style.px(10)
                    Txt {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        muted: true
                        font.pixelSize: Style.fontLead
                        text: results.busy ? qsTr("Searching…")
                            : results.error ? qsTr("Search failed: %1").arg(results.error)
                            : view.instanceId.length === 0 ? qsTr("Pick an instance first")
                            : queryField.text ? qsTr("Nothing found for “%1”").arg(queryField.text)
                            : qsTr("Nothing found")
                    }
                    QuietButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: !results.busy && !!results.error
                        text: qsTr("Try again")
                        iconName: "refresh"
                        onClicked: results.search()
                    }
                }
            }

            ResDetails {
                visible: results.selectedRow >= 0
                search: results
                Layout.preferredWidth: Math.min(Style.px(380), view.width * 0.42)
                Layout.fillHeight: true
            }
        }

        // Ticked projects: a tray with one install button for all of them
        Rectangle {
            Layout.fillWidth: true
            visible: results.pickedCount > 0 || results.queuedCount > 0
            implicitHeight: trayRow.implicitHeight + Style.px(20)
            radius: Style.radius
            color: Style.panel(1)
            border.width: 1
            border.color: Style.panelBorder

            RowLayout {
                id: trayRow
                anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(10) }
                spacing: Style.px(10)

                // Icons of what is ticked; click one to untick it
                ListView {
                    id: trayList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Style.px(36)
                    visible: results.pickedCount > 0
                    orientation: ListView.Horizontal
                    spacing: Style.px(6)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: results.picked
                    delegate: AbstractButton {
                        required property var modelData
                        width: Style.px(36); height: Style.px(36)
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("%1 — click to remove").arg(modelData.title)
                        Accessible.name: qsTr("Remove %1").arg(modelData.title)
                        onClicked: results.unpick(modelData.projectId)
                        background: Rectangle {
                            radius: Style.radius - 2
                            color: Style.alpha(Style.text, 0.06)
                            border.width: parent.hovered ? 2 : 0
                            border.color: Style.danger
                            clip: true
                            Image {
                                id: trayIcon
                                anchors.fill: parent
                                anchors.margins: 1
                                source: modelData.iconUrl
                                sourceSize: Qt.size(72, 72)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                visible: status === Image.Ready
                            }
                            Icon { anchors.centerIn: parent; visible: !trayIcon.visible; name: "cube"; size: Style.px(16); color: Style.textMuted }
                        }
                    }
                }
                Txt {
                    Layout.fillWidth: !trayList.visible
                    muted: true
                    text: results.pickedCount > 0 ? qsTr("%n selected", "", results.pickedCount)
                        : qsTr("%n more in the queue", "", results.queuedCount)
                }
                QuietButton {
                    visible: results.pickedCount > 0
                    plain: true
                    text: qsTr("Clear")
                    onClicked: results.clearPicks()
                }
                PrimaryButton {
                    visible: results.pickedCount > 0
                    text: qsTr("Install selected (%1)").arg(results.pickedCount)
                    iconName: "plus"
                    onClicked: results.installPicked()
                }
            }
        }
    }
}
