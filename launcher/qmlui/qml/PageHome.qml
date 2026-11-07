import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// Home: the selected instance on the full backdrop, with labeled actions and the quick-switch hotbar.
Item {
    id: page
    property var params: ({})
    property var current: null
    property int selectedIndex: -1

    function refresh() {
        let i = Instances.indexOf(Router.selectedId)
        if (i < 0 && Instances.count > 0) { i = 0; Router.selectedId = Instances.get(0).instanceId }
        selectedIndex = i
        current = i >= 0 ? Instances.get(i) : null
    }
    function select(i) {
        if (i < 0 || i >= Instances.count) return
        Router.selectedId = Instances.get(i).instanceId
        refresh()
    }
    Component.onCompleted: { if (Instances.filter !== "") Instances.filter = ""; refresh(); forceActiveFocus() }
    Connections {
        target: Router
        function onSelectedIdChanged() { if (!page.current || page.current.instanceId !== Router.selectedId) page.refresh() }
    }
    Connections {
        target: Instances
        function onCountChanged() { page.refresh() }
        function onDataChanged() { page.refresh() }
        function onLayoutChanged() { page.refresh() }
        function onModelReset() { page.refresh() }
    }

    focus: true
    Keys.onPressed: (event) => {
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) { select(event.key - Qt.Key_1); event.accepted = true }
        else if (event.key === Qt.Key_Right) { select(Math.min(Instances.count - 1, selectedIndex + 1)); event.accepted = true }
        else if (event.key === Qt.Key_Left) { select(Math.max(0, selectedIndex - 1)); event.accepted = true }
        else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && current) { Launcher.launch(current.instanceId); event.accepted = true }
    }

    // Title block — the one orchestrated motion: it rises in when the selection changes.
    ColumnLayout {
        id: hero
        visible: page.current !== null
        anchors { left: parent.left; right: parent.right; bottom: actions.top
                  leftMargin: Style.px(48); rightMargin: Style.px(48); bottomMargin: Style.px(24) }
        spacing: Style.px(10)

        Txt {
            visible: !!(page.current && page.current.packName && page.current.packName !== page.current.name)
            text: page.current ? page.current.packName : ""
            muted: true
            font.pixelSize: Style.fontLead
        }
        // The instance name, set like a title screen: a hard stepped shadow (no blur), the way Minecraft draws text
        Item {
            Layout.fillWidth: true
            implicitHeight: title.implicitHeight + titleShadow.step
            Text {
                id: titleShadow
                readonly property real step: Math.max(2, Math.round(Style.fontHero / 16))
                x: step; y: step
                width: title.width
                text: title.text
                font: title.font
                lineHeight: title.lineHeight
                wrapMode: title.wrapMode
                maximumLineCount: title.maximumLineCount
                elide: title.elide
                color: Style.dark ? Qt.rgba(0, 0, 0, 0.55) : Style.alpha(Style.accent, 0.28)
            }
            Text {
                id: title
                width: parent.width
                text: page.current ? page.current.name : ""
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.fontHero
                font.weight: Font.DemiBold
                font.letterSpacing: -0.5
                lineHeight: 1.05
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        RowLayout {
            spacing: Style.px(28)
            Txt { text: Fmt.versionLine(page.current); visible: text.length > 0; font.pixelSize: Style.fontLead }
            Txt { text: page.current ? Fmt.playTime(page.current.playTime) : ""; visible: text.length > 0; muted: true; font.pixelSize: Style.fontLead }
            Txt { text: page.current ? Fmt.lastPlayed(page.current.lastPlayed) : ""; visible: text.length > 0; muted: true; font.pixelSize: Style.fontLead }
        }

        transform: Translate { id: rise; y: 0 }
        ParallelAnimation {
            id: riseIn
            NumberAnimation { target: rise; property: "y"; from: Style.px(14); to: 0; duration: Style.dur(380); easing.type: Easing.OutCubic }
            NumberAnimation { target: hero; property: "opacity"; from: 0; to: 1; duration: Style.dur(320); easing.type: Easing.OutCubic }
        }
    }
    onSelectedIndexChanged: if (Style.motion) riseIn.restart()

    // Empty state
    ColumnLayout {
        visible: page.current === null
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: Style.px(48) }
        spacing: Style.px(16)
        Text {
            text: qsTr("Nothing to play yet")
            color: Style.text
            font.family: Style.display
            font.pixelSize: Style.fontHero
            font.weight: Font.DemiBold
        }
        Txt {
            text: qsTr("Create an instance: pick a Minecraft version, or install a modpack from Modrinth or CurseForge.")
            muted: true
            font.pixelSize: Style.fontLead
            wrapMode: Text.WordWrap
            Layout.maximumWidth: Style.px(520)
        }
        PrimaryButton { text: qsTr("Create instance"); iconName: "plus"; onClicked: Router.open("create") }
    }

    // Actions — every button says what it does
    RowLayout {
        id: actions
        visible: page.current !== null
        anchors { left: parent.left; bottom: hotbarRow.visible ? hotbarRow.top : parent.bottom
                  leftMargin: Style.px(48); bottomMargin: Style.px(hotbarRow.visible ? 36 : 48) }
        spacing: Style.px(10)

        PlayButton { instanceId: page.current ? page.current.instanceId : ""; Layout.preferredWidth: Style.px(220) }
        ContinueButton { instanceId: page.current ? page.current.instanceId : ""; Layout.maximumWidth: Style.px(300) }
        QuietButton { iconName: "cube"; text: qsTr("Mods"); onClicked: Router.openInstance(page.current.instanceId, "mods") }
        QuietButton { iconName: "folder"; text: qsTr("Folder"); onClicked: Launcher.openInstanceFolder(page.current.instanceId) }
        QuietButton { iconName: "sliders"; text: qsTr("Manage"); onClicked: Router.openInstance(page.current.instanceId, "overview") }
        FriendsHomeButton {}
    }

    // Servers of this instance (status, quick join), above the actions on the right
    HomeServers {
        instanceId: page.current ? page.current.instanceId : ""
        anchors { right: parent.right; bottom: actions.bottom; rightMargin: Style.px(48) }
        width: Math.min(parent.width * 0.38, Style.px(420))
    }

    // Hotbar: quick switch between the 9 most recent instances
    Rectangle {
        id: hotbarRow
        visible: Style.showHotbar && Instances.count > 0
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Style.px(24) }
        width: slots.implicitWidth + Style.px(12)
        height: slots.implicitHeight + Style.px(12)
        radius: Style.slotRadius + Style.px(6)
        color: Style.alpha(Style.bg, 0.55)
        border.width: 1
        border.color: Style.alpha(Style.text, 0.08)

        Row {
            id: slots
            anchors.centerIn: parent
            spacing: Style.px(6)
            Repeater {
                model: Instances
                delegate: HotbarSlot {
                    visible: index < 9
                    number: index + 1
                    iconSource: model.icon
                    tip: model.name
                    selected: index === page.selectedIndex
                    running: model.running || Launcher.launches[model.instanceId] !== undefined
                    onClicked: page.select(index)
                    onDoubleClicked: Launcher.launch(model.instanceId)
                }
            }
            HotbarSlot {
                glyph: "grid"
                tip: qsTr("All instances")
                onClicked: Router.go("instances")
            }
        }
    }
}
