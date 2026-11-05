import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// The stage: selected instance title, its actions, and the hotbar.
Item {
    id: page
    property var current: null              // map from Instances.get()
    property int selectedIndex: -1
    signal select(int index)
    signal openLibrary()

    readonly property var launch: current ? Launcher.launches[current.instanceId] : undefined
    readonly property bool busy: launch !== undefined

    // Title block — the one orchestrated motion: it rises in when the selection changes.
    ColumnLayout {
        id: hero
        visible: page.current !== null
        anchors { left: parent.left; right: parent.right; bottom: actions.top
                  leftMargin: Style.px(56); rightMargin: Style.px(56); bottomMargin: Style.px(24) }
        spacing: Style.px(10)

        Txt {
            visible: !!(page.current && page.current.packName && page.current.packName !== page.current.name)
            text: page.current ? page.current.packName : ""
            muted: true
            font.pixelSize: Style.fontLead
        }
        Text {
            id: title
            Layout.fillWidth: true
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
        anchors { left: parent.left; bottom: hotbarRow.top; leftMargin: Style.px(56); bottomMargin: Style.px(56) }
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
        PrimaryButton { text: qsTr("Create instance"); iconName: "plus"; onClicked: Launcher.newInstance() }
    }

    // Actions
    RowLayout {
        id: actions
        visible: page.current !== null
        anchors { left: parent.left; bottom: hotbarRow.top; leftMargin: Style.px(56); bottomMargin: Style.px(44) }
        spacing: Style.px(10)

        PrimaryButton {
            id: playButton
            objectName: "playButton"
            Layout.preferredWidth: Style.px(220)
            readonly property string phase: page.launch ? page.launch.phase : ""
            text: phase === "running" ? qsTr("Stop") : phase === "launching" ? page.launch.status : qsTr("Play")
            iconName: phase === "running" ? "stop" : phase === "launching" ? "" : "play"
            progress: phase === "launching" ? page.launch.progress : undefined
            fill: phase === "running" ? Style.surfaceHi : Style.accent
            ink: phase === "running" ? Style.text : Style.onAccent
            onClicked: {
                if (!page.current) return
                if (phase === "") Launcher.launch(page.current.instanceId)
                else Launcher.kill(page.current.instanceId)
            }
        }
        QuietButton { iconName: "cube"; text: qsTr("Mods"); onClicked: Launcher.editInstance(page.current.instanceId, "mods") }
        QuietButton { iconName: "folder"; tip: qsTr("Open folder"); onClicked: Launcher.openInstanceFolder(page.current.instanceId) }
        QuietButton { iconName: "sliders"; tip: qsTr("Edit instance"); onClicked: Launcher.editInstance(page.current.instanceId, "") }
        QuietButton {
            iconName: "more"
            tip: qsTr("More actions")
            onClicked: moreMenu.popup()
            ThemedMenu {
                id: moreMenu
                ThemedMenuItem { text: qsTr("Console"); iconName: "terminal"; onTriggered: Launcher.showConsole(page.current.instanceId) }
                ThemedMenuItem { text: qsTr("Launch offline"); iconName: "play"; onTriggered: Launcher.launch(page.current.instanceId, true) }
                ThemedMenuItem { text: qsTr("Screenshots folder"); iconName: "image"; onTriggered: Launcher.openScreenshotsFolder(page.current.instanceId) }
                ThemedMenuItem { text: qsTr("Change icon"); iconName: "pencil"; onTriggered: Launcher.changeIcon(page.current.instanceId) }
                ThemedMenuItem { text: qsTr("Copy instance"); iconName: "copy"; onTriggered: Launcher.copyInstance(page.current.instanceId) }
                ThemedMenuItem { text: qsTr("Export"); iconName: "export"; onTriggered: Launcher.exportInstance(page.current.instanceId) }
                ThemedMenuItem { text: qsTr("Delete"); iconName: "trash"; danger: true; onTriggered: Launcher.deleteInstance(page.current.instanceId) }
            }
        }
    }

    // Hotbar
    Rectangle {
        id: hotbarRow
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Style.px(28) }
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
                visible: Instances.count < 9
                glyph: "plus"
                tip: qsTr("Create instance")
                onClicked: Launcher.newInstance()
            }
            HotbarSlot {
                glyph: "grid"
                tip: qsTr("All instances")
                onClicked: page.openLibrary()
            }
        }
    }
}
