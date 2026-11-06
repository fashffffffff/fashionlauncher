import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Minecraft version" source: version list, then mod loader and its version.
Item {
    id: tab
    property string mcVersion: ""
    property string loader: ""
    property string loaderVersion: ""
    readonly property var mc: Creator.versions
    readonly property var lv: Creator.loaderVersions

    readonly property string suggestedName: mcVersion.length === 0 ? "" : loader.length ? mcVersion + " " + Creator.loaderName : mcVersion
    readonly property bool ready: mcVersion.length > 0 && (loader.length === 0 || loaderVersion.length > 0)
    readonly property string iconSource: ""
    readonly property string autoIconLabel: ""
    readonly property string blocker: mcVersion.length === 0 ? qsTr("Pick a Minecraft version.")
                                    : loader.length && !loaderVersion.length ? Creator.loaderNote : ""
    function create(name, icon, group) { return Creator.createVanilla(mcVersion, loaderVersion, name, icon, group) }

    readonly property var loaderOptions: [
        { value: "", label: qsTr("None") },
        { value: "fabric", label: "Fabric" },
        { value: "quilt", label: "Quilt" },
        { value: "forge", label: "Forge" },
        { value: "neoforge", label: "NeoForge" }
    ]
    readonly property var typeNames: ({
        snapshot: qsTr("Snapshot"), experiment: qsTr("Experiment"), old_beta: qsTr("Beta"), old_alpha: qsTr("Alpha")
    })

    function pickDefaultMc() { if (mc.indexOf(mcVersion) < 0 && mcVersion.length === 0) mcVersion = mc.recommended }
    function pickDefaultLoader() { if (lv.indexOf(loaderVersion) < 0) loaderVersion = lv.recommended }
    function refreshLoader() { Creator.setLoader(loader, mcVersion); pickDefaultLoader() }

    Component.onCompleted: { Creator.init(); pickDefaultMc(); refreshLoader() }
    onMcVersionChanged: { refreshLoader(); Qt.callLater(() => mcList.positionViewAtIndex(mc.indexOf(mcVersion), ListView.Contain)) }
    onLoaderVersionChanged: Qt.callLater(() => loaderList.positionViewAtIndex(lv.indexOf(loaderVersion), ListView.Contain))
    onLoaderChanged: { loaderVersion = ""; refreshLoader() }
    Connections { target: tab.mc; function onCountChanged() { tab.pickDefaultMc() } }
    Connections { target: tab.lv; function onCountChanged() { tab.pickDefaultLoader() } }

    // Shared look for an empty / loading / failed list
    component ListState: ColumnLayout {
        id: ls
        property bool loading
        property string error
        property string empty
        property var onRetry
        anchors.centerIn: parent
        width: parent.width - Style.px(40)
        spacing: Style.px(10)
        BusyIndicator { visible: ls.loading; running: visible; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Style.px(36); Layout.preferredHeight: Style.px(36) }
        Txt {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            muted: !ls.error.length
            color: ls.error.length ? Style.danger : Style.textMuted
            text: ls.loading ? qsTr("Loading versions…") : ls.error.length ? qsTr("Couldn't load the list: %1").arg(ls.error) : ls.empty
        }
        QuietButton { visible: !ls.loading && ls.error.length > 0; text: qsTr("Try again"); iconName: "refresh"; Layout.alignment: Qt.AlignHCenter; onClicked: ls.onRetry() }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Style.px(24)

        // Step 1: Minecraft version
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(12)
            CreateStep { number: 1; title: qsTr("Minecraft version"); Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(8)
                Field {
                    id: mcSearch
                    Layout.fillWidth: true
                    iconName: "search"
                    placeholderText: qsTr("Search versions")
                    onTextChanged: tab.mc.search = text
                    Keys.onEscapePressed: text = ""
                    Accessible.name: qsTr("Search Minecraft versions")
                }
                CreateChip { text: qsTr("Snapshots"); checked: tab.mc.showSnapshots; onToggled: tab.mc.showSnapshots = checked }
                CreateChip { text: qsTr("Old versions"); checked: tab.mc.showOld; onToggled: tab.mc.showOld = checked }
                QuietButton { iconName: "refresh"; tip: qsTr("Reload the version list"); onClicked: tab.mc.reload(true) }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radius
                color: Style.panel(0)
                border.width: 1
                border.color: Style.panelBorder
                ListView {
                    id: mcList
                    anchors { fill: parent; margins: Style.px(6) }
                    clip: true
                    model: tab.mc
                    spacing: Style.px(2)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: CreateListRow {
                        selected: model.version === tab.mcVersion
                        Accessible.name: model.version
                        onClicked: tab.mcVersion = model.version
                        contentItem: RowLayout {
                            spacing: Style.px(10)
                            anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                            Txt { text: model.version; font.weight: Font.Medium; Layout.fillWidth: true }
                            CreateBadge { visible: model.recommended; text: qsTr("Recommended") }
                            CreateBadge { visible: model.type !== "release"; text: tab.typeNames[model.type] || model.type; tint: Style.textMuted }
                            Txt { text: model.date; muted: true; font.pixelSize: Style.fontSmall }
                        }
                    }
                    ListState {
                        visible: tab.mc.count === 0
                        loading: tab.mc.loading
                        error: tab.mc.error
                        empty: mcSearch.text.length ? qsTr("No versions match “%1”.").arg(mcSearch.text) : qsTr("No versions to show.")
                        onRetry: () => tab.mc.reload(true)
                    }
                }
            }
        }

        // Step 2: mod loader
        ColumnLayout {
            Layout.preferredWidth: Style.px(430)
            Layout.fillHeight: true
            spacing: Style.px(12)
            CreateStep {
                number: 2
                title: qsTr("Mod loader")
                hint: qsTr("Needed to play with mods. Pick “None” for the plain game.")
                Layout.fillWidth: true
            }
            CreateSegmented {
                options: tab.loaderOptions
                value: tab.loader
                onPicked: v => tab.loader = v
            }
            Txt {
                visible: tab.loader.length > 0
                text: qsTr("%1 version").arg(Creator.loaderName)
                font.weight: Font.Medium
                Layout.topMargin: Style.px(6)
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radius
                color: Style.panel(0)
                border.width: 1
                border.color: Style.panelBorder
                visible: tab.loader.length > 0
                ListView {
                    id: loaderList
                    anchors { fill: parent; margins: Style.px(6) }
                    clip: true
                    model: tab.lv
                    spacing: Style.px(2)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: CreateListRow {
                        selected: model.version === tab.loaderVersion
                        Accessible.name: model.version
                        onClicked: tab.loaderVersion = model.version
                        contentItem: RowLayout {
                            spacing: Style.px(10)
                            anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                            Txt { text: model.version; font.weight: Font.Medium; Layout.fillWidth: true }
                            CreateBadge { visible: model.recommended; text: qsTr("Recommended") }
                            Txt { text: model.date; muted: true; font.pixelSize: Style.fontSmall }
                        }
                    }
                    ListState {
                        visible: tab.lv.count === 0
                        loading: tab.lv.loading
                        error: tab.lv.error
                        empty: Creator.loaderNote
                        onRetry: () => tab.lv.reload(true)
                    }
                }
            }
            // No loader: say what that means instead of an empty box
            Rectangle {
                visible: tab.loader.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radius
                color: "transparent"
                border.width: 1
                border.color: Style.panelBorder
                Txt {
                    anchors.centerIn: parent
                    width: parent.width - Style.px(48)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    muted: true
                    text: qsTr("The game as Mojang ships it. You can add a mod loader later.")
                }
            }
        }
    }
}
