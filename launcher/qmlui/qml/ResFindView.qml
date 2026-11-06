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
        RowLayout {
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
            Item { Layout.fillWidth: true }
            // What the results are limited to
            Txt {
                visible: view.kind === "mods" && results.gameVersion.length > 0
                muted: true
                text: results.loaderText ? qsTr("For Minecraft %1 with %2").arg(results.gameVersion).arg(results.loaderText)
                                         : qsTr("For Minecraft %1").arg(results.gameVersion)
            }
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
                        onInstallRequested: results.install(index, -1)
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
    }
}
