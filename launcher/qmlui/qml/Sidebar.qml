import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Labeled navigation. Floats over the backdrop as a panel; compact mode shows icons only.
Rectangle {
    id: bar
    readonly property bool compact: Style.navCompact
    width: compact ? Style.px(76) : Style.px(236)
    radius: Style.radius + Style.px(4)
    color: Style.panel(0)
    border.width: 1
    border.color: Style.panelBorder
    Behavior on width { NumberAnimation { duration: Style.dur(180); easing.type: Easing.OutCubic } }

    readonly property var items: [
        { id: "home", icon: "home", label: qsTr("Home") },
        { id: "instances", icon: "grid", label: qsTr("Instances") },
        { id: "browse", icon: "compass", label: qsTr("Browse mods") },
        { id: "friends", icon: "user", label: qsTr("Friends") },
        { id: "appearance", icon: "palette", label: qsTr("Appearance") },
        { id: "settings", icon: "gear", label: qsTr("Settings") }
    ]

    component NavButton: AbstractButton {
        id: nb
        property string iconName
        property bool active: false
        onClicked: Sounds.play("click")
        Layout.fillWidth: true
        implicitHeight: Style.px(44)
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: text
        ToolTip.visible: bar.compact && hovered
        ToolTip.text: text
        ToolTip.delay: 300
        background: Rectangle {
            radius: Style.radius
            color: nb.active ? Style.alpha(Style.accent, 0.18) : nb.hovered ? Style.alpha(Style.text, 0.07) : "transparent"
            border.width: nb.visualFocus ? 2 : 0
            border.color: Style.accent
            Rectangle {   // active marker
                visible: nb.active
                width: Style.px(3); height: parent.height * 0.5; radius: 2
                anchors { left: parent.left; leftMargin: Style.px(4); verticalCenter: parent.verticalCenter }
                color: Style.accent
            }
        }
        contentItem: Item {
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: bar.compact ? 0 : Style.px(14)
                spacing: Style.px(12)
                Item { visible: bar.compact; Layout.fillWidth: true }
                Icon {
                    name: nb.iconName
                    size: Style.px(20)
                    color: nb.active ? Style.accent : Style.text
                }
                Text {
                    visible: !bar.compact
                    Layout.fillWidth: true
                    text: nb.text
                    color: Style.text
                    font.family: Style.ui
                    font.pixelSize: Style.fontBody + 1
                    font.weight: nb.active ? Font.DemiBold : Font.Medium
                    elide: Text.ElideRight
                }
                Item { visible: bar.compact; Layout.fillWidth: true }
            }
        }
    }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(12) }
        spacing: Style.px(4)

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Style.px(6)
            Layout.bottomMargin: Style.px(16)
            Layout.leftMargin: bar.compact ? 0 : Style.px(6)
            spacing: Style.px(10)
            Item { visible: bar.compact; Layout.fillWidth: true }
            Image {
                source: Launcher.logo
                Layout.preferredWidth: Style.px(30); Layout.preferredHeight: Style.px(30)
                sourceSize: Qt.size(96, 96)
                smooth: true
            }
            Text {
                visible: !bar.compact
                Layout.fillWidth: true
                text: Launcher.appName
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(15)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Item { visible: bar.compact; Layout.fillWidth: true }
        }

        // Search: opens the command palette
        AbstractButton {
            id: searchBtn
            Layout.fillWidth: true
            Layout.bottomMargin: Style.px(10)
            implicitHeight: Style.px(38)
            hoverEnabled: true
            Accessible.name: qsTr("Search")
            ToolTip.visible: bar.compact && hovered
            ToolTip.text: qsTr("Search (%1)").arg(Qt.platform.os === "osx" ? "⌘K" : "Ctrl+K")
            onClicked: Router.openPalette()
            background: Rectangle {
                radius: Style.radius
                color: searchBtn.hovered ? Style.alpha(Style.text, 0.10) : Style.alpha(Style.text, 0.05)
                border.width: searchBtn.visualFocus ? 2 : 1
                border.color: searchBtn.visualFocus ? Style.accent : Style.alpha(Style.text, 0.06)
            }
            contentItem: RowLayout {
                anchors { fill: parent; leftMargin: bar.compact ? 0 : Style.px(12); rightMargin: Style.px(10) }
                spacing: Style.px(10)
                Item { visible: bar.compact; Layout.fillWidth: true }
                Icon { name: "search"; size: Style.px(17); color: Style.textMuted }
                Txt { visible: !bar.compact; text: qsTr("Search"); muted: true; Layout.fillWidth: true }
                Txt { visible: !bar.compact; text: Qt.platform.os === "osx" ? "⌘K" : "Ctrl+K"; muted: true; font.pixelSize: Style.fontSmall }
                Item { visible: bar.compact; Layout.fillWidth: true }
            }
        }

        Repeater {
            model: bar.items
            delegate: NavButton {
                text: modelData.label
                iconName: modelData.icon
                active: Router.section === modelData.id
                onClicked: Router.go(modelData.id)
            }
        }

        // Recently played: one click opens, the small button plays
        Txt {
            visible: Instances.count > 0 && Instances.filter === ""
            Layout.topMargin: Style.px(18)
            Layout.leftMargin: bar.compact ? 0 : Style.px(12)
            Layout.alignment: bar.compact ? Qt.AlignHCenter : Qt.AlignLeft
            text: bar.compact ? "" : qsTr("Recent")
            muted: true
            font.pixelSize: Style.fontSmall
        }
        Repeater {
            model: Instances.filter === "" ? Instances : null
            delegate: AbstractButton {
                id: recent
                visible: index < 3
                Layout.fillWidth: true
                implicitHeight: Style.px(40)
                hoverEnabled: true
                Accessible.name: model.name
                ToolTip.visible: bar.compact && hovered
                ToolTip.text: model.name
                onClicked: Router.openInstance(model.instanceId)
                background: Rectangle {
                    radius: Style.radius
                    color: recent.hovered ? Style.alpha(Style.text, 0.07) : "transparent"
                }
                contentItem: RowLayout {
                    anchors { fill: parent; leftMargin: bar.compact ? 0 : Style.px(10); rightMargin: Style.px(6) }
                    spacing: Style.px(10)
                    Item { visible: bar.compact; Layout.fillWidth: true }
                    Image {
                        source: model.icon
                        Layout.preferredWidth: Style.px(24); Layout.preferredHeight: Style.px(24)
                        sourceSize: Qt.size(48, 48)
                        smooth: false
                    }
                    Txt { visible: !bar.compact; text: model.name; Layout.fillWidth: true }
                    AbstractButton {
                        id: rp
                        visible: !bar.compact && (recent.hovered || st !== undefined)
                        readonly property var st: Launcher.launches[model.instanceId]
                        Layout.preferredWidth: Style.px(28); Layout.preferredHeight: Style.px(28)
                        hoverEnabled: true
                        Accessible.name: st ? qsTr("Stop") : qsTr("Play")
                        onClicked: { Sounds.play(st ? "click" : "launch"); st ? Launcher.kill(model.instanceId) : Launcher.launch(model.instanceId) }
                        background: Rectangle { radius: width / 2; color: rp.st ? Style.success : Style.accent }
                        contentItem: Item { Icon { anchors.centerIn: parent; name: rp.st ? "stop" : "play"; size: Style.px(12); color: Style.onAccent } }
                    }
                    Item { visible: bar.compact; Layout.fillWidth: true }
                }
            }
        }

        Item { Layout.fillHeight: true }

        PrimaryButton {
            Layout.fillWidth: true
            implicitHeight: Style.px(42)
            text: bar.compact ? "" : qsTr("Create instance")
            iconName: "plus"
            Accessible.name: qsTr("Create instance")
            onClicked: Router.open("create")
        }

        // Account
        AbstractButton {
            id: acc
            Layout.fillWidth: true
            Layout.topMargin: Style.px(8)
            implicitHeight: Style.px(54)
            hoverEnabled: true
            Accessible.name: qsTr("Accounts")
            ToolTip.visible: bar.compact && hovered
            ToolTip.text: Accounts.current.name || qsTr("Add account")
            onClicked: Router.openAccounts()
            background: Rectangle {
                radius: Style.radius
                color: acc.hovered ? Style.alpha(Style.text, 0.08) : Style.alpha(Style.text, 0.04)
                border.width: acc.visualFocus ? 2 : 0
                border.color: Style.accent
            }
            contentItem: RowLayout {
                anchors { fill: parent; leftMargin: bar.compact ? 0 : Style.px(10); rightMargin: Style.px(10) }
                spacing: Style.px(10)
                Item { visible: bar.compact; Layout.fillWidth: true }
                Image {
                    visible: !!Accounts.current.face
                    source: Accounts.current.face || ""
                    Layout.preferredWidth: Style.px(30); Layout.preferredHeight: Style.px(30)
                    sourceSize: Qt.size(60, 60)
                    smooth: false
                }
                Icon { visible: !Accounts.current.face; name: "user"; size: Style.px(22) }
                ColumnLayout {
                    visible: !bar.compact
                    Layout.fillWidth: true
                    spacing: 0
                    Txt { text: Accounts.current.name || qsTr("Add account"); font.weight: Font.Medium; Layout.fillWidth: true }
                    Txt {
                        visible: !!Accounts.current.kind
                        text: Accounts.current.kind === "msa" ? qsTr("Microsoft") : qsTr("Offline")
                        muted: true
                        font.pixelSize: Style.fontSmall
                    }
                }
                Icon { visible: !bar.compact; name: "chevron-right"; size: Style.px(16); color: Style.textMuted }
                Item { visible: bar.compact; Layout.fillWidth: true }
            }
        }
    }
}
