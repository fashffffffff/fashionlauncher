import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Modpack" source: search Modrinth or CurseForge, pick a pack, pick its version.
Item {
    id: tab
    readonly property var packs: Creator.packs
    readonly property var pack: Creator.pack
    property int selectedRow: -1
    property int versionIndex: -1          // index into the pack's full version list (from packVersions[].index)
    property bool started: false

    readonly property bool hasPack: pack && pack.packId !== undefined
    readonly property string suggestedName: hasPack ? pack.title : ""
    readonly property bool ready: hasPack && versionIndex >= 0
    readonly property string iconSource: hasPack ? pack.iconUrl : ""
    readonly property string autoIconLabel: qsTr("Use the modpack's logo")
    readonly property string blocker: !hasPack ? qsTr("Pick a modpack.")
                                    : Creator.packVersionsLoading ? qsTr("Loading versions…")
                                    : versionIndex < 0 ? (Creator.packError || qsTr("Pick a version.")) : ""
    function create(name, icon, group) { return Creator.createModpack(versionIndex, name, icon, group) }
    function search(term) { selectedRow = -1; Creator.clearPack(); packs.search(term) }

    function start() { if (!started) { started = true; search("") } }
    onVisibleChanged: if (visible) start()
    Component.onCompleted: if (visible) start()

    Connections {
        target: Creator
        function onPackChanged() {
            const list = Creator.packVersions
            if (!list.some(v => v.index === tab.versionIndex)) {
                const stable = list.find(v => v.type !== "Beta" && v.type !== "Alpha")
                tab.versionIndex = stable ? stable.index : list.length ? list[0].index : -1
            }
        }
    }

    Timer { id: debounce; interval: 450; onTriggered: tab.search(searchField.text) }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(12)

        CreateStep { number: 1; title: qsTr("Find a modpack"); Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(10)
            CreateSegmented {
                options: [{ value: "modrinth", label: "Modrinth" }, { value: "curseforge", label: "CurseForge" }]
                value: tab.packs.provider
                itemHeight: Style.px(32)
                onPicked: v => { tab.selectedRow = -1; Creator.clearPack(); tab.packs.provider = v }
            }
            Field {
                id: searchField
                Layout.fillWidth: true
                iconName: "search"
                placeholderText: qsTr("Search modpacks")
                Accessible.name: qsTr("Search modpacks")
                onTextEdited: debounce.restart()
                onAccepted: { debounce.stop(); tab.search(text) }
                Keys.onEscapePressed: { text = ""; debounce.restart() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(24)

            // Results
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: Style.px(400)
                radius: Style.radius
                color: Style.panel(0)
                border.width: 1
                border.color: Style.panelBorder
                ListView {
                    id: results
                    anchors { fill: parent; margins: Style.px(6) }
                    clip: true
                    model: tab.packs
                    spacing: Style.px(2)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    onAtYEndChanged: if (atYEnd && count > 0) tab.packs.loadMore()
                    delegate: CreateListRow {
                        implicitHeight: Style.px(76)
                        selected: index === tab.selectedRow
                        Accessible.name: model.title
                        onClicked: { tab.selectedRow = index; tab.versionIndex = -1; Creator.selectPack(index) }
                        contentItem: RowLayout {
                            spacing: Style.px(12)
                            anchors { fill: parent; leftMargin: Style.px(10); rightMargin: Style.px(12) }
                            Rectangle {
                                Layout.preferredWidth: Style.px(52)
                                Layout.preferredHeight: Style.px(52)
                                radius: Style.radius - 4
                                color: Style.alpha(Style.text, 0.06)
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    source: model.iconUrl
                                    sourceSize: Qt.size(104, 104)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Style.px(2)
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Style.px(8)
                                    Txt { text: model.title; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                    Txt {
                                        text: qsTr("%1 downloads").arg(Creator.formatCount(model.downloads))
                                        muted: true
                                        font.pixelSize: Style.fontSmall
                                    }
                                }
                                Txt {
                                    visible: model.author.length > 0
                                    text: qsTr("by %1").arg(model.author)
                                    muted: true
                                    font.pixelSize: Style.fontSmall
                                    Layout.fillWidth: true
                                }
                                Txt {
                                    text: model.summary
                                    muted: true
                                    Layout.fillWidth: true
                                    font.pixelSize: Style.fontSmall
                                }
                            }
                        }
                    }
                    footer: Item {
                        width: results.width
                        height: tab.packs.loading && results.count > 0 ? Style.px(48) : 0
                        BusyIndicator { anchors.centerIn: parent; visible: parent.height > 0; running: visible; width: Style.px(28); height: width }
                    }
                    ColumnLayout {
                        visible: results.count === 0
                        anchors.centerIn: parent
                        width: parent.width - Style.px(40)
                        spacing: Style.px(10)
                        BusyIndicator { visible: tab.packs.loading; running: visible; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Style.px(36); Layout.preferredHeight: Style.px(36) }
                        Txt {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: tab.packs.error.length ? Style.danger : Style.textMuted
                            text: tab.packs.loading ? qsTr("Searching…")
                                : tab.packs.error.length ? qsTr("Couldn't search: %1").arg(tab.packs.error)
                                : tab.started ? qsTr("No modpacks found.") : ""
                        }
                        QuietButton {
                            visible: !tab.packs.loading && tab.packs.error.length > 0
                            text: qsTr("Try again"); iconName: "refresh"
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: tab.search(searchField.text)
                        }
                    }
                }
            }

            // Selected pack + version
            ColumnLayout {
                Layout.preferredWidth: Style.px(380)
                Layout.maximumWidth: Style.px(380)
                Layout.fillWidth: false
                Layout.fillHeight: true
                spacing: Style.px(12)

                Txt {
                    visible: !tab.hasPack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                    muted: true
                    text: qsTr("Pick a modpack from the list to see its versions.")
                }

                RowLayout {
                    visible: tab.hasPack
                    Layout.fillWidth: true
                    spacing: Style.px(14)
                    Rectangle {
                        Layout.preferredWidth: Style.px(64)
                        Layout.preferredHeight: Style.px(64)
                        radius: Style.radius - 2
                        color: Style.alpha(Style.text, 0.06)
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: tab.hasPack ? tab.pack.iconUrl : ""
                            sourceSize: Qt.size(128, 128)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(2)
                        Text {
                            text: tab.hasPack ? tab.pack.title : ""
                            color: Style.text
                            font.family: Style.ui
                            font.pixelSize: Style.fontLead
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Txt {
                            text: !tab.hasPack ? "" : (tab.pack.author ? qsTr("by %1").arg(tab.pack.author) + " · " : "")
                                  + qsTr("%1 downloads").arg(Creator.formatCount(tab.pack.downloads))
                            muted: true
                            font.pixelSize: Style.fontSmall
                            Layout.fillWidth: true
                        }
                    }
                }
                Txt {
                    visible: tab.hasPack
                    text: tab.hasPack ? tab.pack.summary : ""
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    muted: true
                }

                CreateStep { visible: tab.hasPack; number: 2; title: qsTr("Version"); hint: qsTr("The newest stable version is picked for you."); Layout.fillWidth: true; Layout.topMargin: Style.px(4) }
                Rectangle {
                    visible: tab.hasPack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Style.radius
                    color: Style.panel(0)
                    border.width: 1
                    border.color: Style.panelBorder
                    ListView {
                        id: versionList
                        anchors { fill: parent; margins: Style.px(6) }
                        clip: true
                        model: Creator.packVersions
                        spacing: Style.px(2)
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: CreateListRow {
                            implicitHeight: Style.px(50)
                            selected: modelData.index === tab.versionIndex
                            Accessible.name: modelData.name
                            onClicked: tab.versionIndex = modelData.index
                            contentItem: ColumnLayout {
                                spacing: 0
                                anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12); topMargin: Style.px(6); bottomMargin: Style.px(6) }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Txt { text: modelData.name; font.weight: Font.Medium; Layout.fillWidth: true }
                                    CreateBadge {
                                        visible: modelData.type.length > 0 && modelData.type !== "Release"
                                        text: modelData.type === "Beta" ? qsTr("Beta") : modelData.type === "Alpha" ? qsTr("Alpha") : modelData.type
                                        tint: Style.textMuted
                                    }
                                }
                                Txt {
                                    text: (modelData.minecraft.length ? qsTr("Minecraft %1").arg(modelData.minecraft) : "")
                                          + (modelData.date.length ? (modelData.minecraft.length ? " · " : "") + modelData.date : "")
                                    muted: true
                                    font.pixelSize: Style.fontSmall
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        ColumnLayout {
                            visible: versionList.count === 0
                            anchors.centerIn: parent
                            width: parent.width - Style.px(32)
                            BusyIndicator { visible: Creator.packVersionsLoading; running: visible; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Style.px(32); Layout.preferredHeight: Style.px(32) }
                            Txt {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                color: Creator.packError.length ? Style.danger : Style.textMuted
                                text: Creator.packVersionsLoading ? qsTr("Loading versions…") : Creator.packError
                            }
                        }
                    }
                }
            }
        }
    }
}
