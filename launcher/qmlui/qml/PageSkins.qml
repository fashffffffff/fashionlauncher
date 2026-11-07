import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Skins: turn your player around in 3D, keep a library of skins, and wear one (Microsoft accounts).
Item {
    id: page
    property var params: ({})
    property string selectedKey
    readonly property var skins: Skins.skins
    readonly property var sel: skins.find(s => s.key === selectedKey) || skins.find(s => s.inUse) || skins[0] || null
    readonly property var selCape: sel && sel.capeId ? Skins.capes.find(c => c.id === sel.capeId) : null

    Connections {
        target: Skins
        function onMessage(text, isError) { Router.toast(text, isError) }
        function onAdded(key) { page.selectedKey = key }
    }

    Component.onCompleted: Skins.load()

    function fileUrl(path) { return !path ? "" : (path.startsWith("/") ? "file://" : "file:///") + path }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(18)

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            QuietButton { plain: true; iconName: "arrow-left"; tip: qsTr("Back"); onClicked: Router.back() }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: qsTr("Skins")
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.px(28)
                    font.weight: Font.DemiBold
                }
                Txt { muted: true; text: qsTr("Turn your player around, collect skins and choose what you wear.") }
            }
            Txt { visible: Skins.accounts.length > 1; text: qsTr("Account"); muted: true }
            SettingsDropdown {
                visible: Skins.accounts.length > 1
                options: Skins.accounts.map(a => ({ value: a.id, label: a.microsoft ? a.name : qsTr("%1 (offline)").arg(a.name) }))
                value: Skins.accountId
                onPicked: (v) => Skins.accountId = v
                implicitWidth: Style.px(220)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(18)

            // Preview and what to do with the selected skin
            Rectangle {
                Layout.preferredWidth: Math.max(Style.px(340), page.width * 0.36)
                Layout.maximumWidth: page.width * 0.5
                Layout.fillHeight: true
                radius: Style.radius + Style.px(4)
                color: Style.panel(0)
                border.width: 1
                border.color: Style.panelBorder

                ColumnLayout {
                    anchors { fill: parent; margins: Style.px(18) }
                    spacing: Style.px(12)

                    SkinViewer {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: Style.px(220)
                        skinPath: page.sel ? page.sel.path : ""
                        slim: page.sel ? page.sel.slim : false
                        capePath: page.selCape ? page.selCape.path : ""

                        Txt {
                            visible: !page.sel
                            anchors.centerIn: parent
                            width: parent.width * 0.8
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            muted: true
                            text: qsTr("No skins yet. Drop a skin PNG here or get one by player name.")
                        }
                        Txt {
                            visible: !!page.sel
                            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                            text: qsTr("Drag to turn · double-click to face front")
                            width: Math.min(implicitWidth, parent.width)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            muted: true
                            font.pixelSize: Style.fontSmall
                            opacity: 0.7
                        }
                    }

                    // Name on its own line, arm model below: long translations must not push the panel wider
                    Flow {
                        visible: !!page.sel
                        Layout.fillWidth: true
                        spacing: Style.px(10)
                        Txt {
                            width: Math.min(implicitWidth, parent.width)
                            height: Style.px(30)
                            verticalAlignment: Text.AlignVCenter
                            text: page.sel ? page.sel.name : ""
                            font.pixelSize: Style.fontLead
                            font.weight: Font.DemiBold
                        }
                        CreateSegmented {
                            itemHeight: Style.px(30)
                            options: [{ value: "classic", label: qsTr("Classic arms") }, { value: "slim", label: qsTr("Slim arms") }]
                            value: page.sel && page.sel.slim ? "slim" : "classic"
                            onPicked: (v) => Skins.setSlim(page.sel.key, v === "slim")
                        }
                    }

                    // Capes the Microsoft account owns
                    ColumnLayout {
                        visible: !!page.sel && Skins.capes.length > 0
                        Layout.fillWidth: true
                        spacing: Style.px(6)
                        Txt { text: qsTr("Cape"); muted: true; font.pixelSize: Style.fontSmall }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Style.px(6)
                            Repeater {
                                model: [{ id: "", name: qsTr("No cape"), path: "" }].concat(Skins.capes)
                                delegate: AbstractButton {
                                    id: capeBtn
                                    required property var modelData
                                    readonly property bool chosen: page.sel && (page.sel.capeId || "") === modelData.id
                                    implicitHeight: Style.px(34)
                                    implicitWidth: capeRow.implicitWidth + Style.px(20)
                                    hoverEnabled: true
                                    onClicked: Skins.setCape(page.sel.key, modelData.id)
                                    background: Rectangle {
                                        radius: Style.radius
                                        color: capeBtn.chosen ? Style.alpha(Style.accent, 0.18) : capeBtn.hovered ? Style.alpha(Style.text, 0.08) : "transparent"
                                        border.width: 1
                                        border.color: capeBtn.chosen ? Style.accent : Style.line
                                    }
                                    contentItem: Item {
                                        Row {
                                            id: capeRow
                                            anchors.centerIn: parent
                                            spacing: Style.px(8)
                                            Image {
                                                visible: !!capeBtn.modelData.path
                                                source: page.fileUrl(capeBtn.modelData.path)
                                                sourceClipRect: Qt.rect(1, 1, 10, 16)
                                                smooth: false
                                                width: Style.px(10); height: Style.px(16)
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Txt { text: capeBtn.modelData.name; anchors.verticalCenter: parent.verticalCenter }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    SettingsNote {
                        visible: !Skins.microsoft
                        text: qsTr("This is an offline account, so the game can't download a skin for it — in game you'll look like Steve or Alex. "
                                 + "You can still keep and preview skins here. To wear one in game, install a skin mod (for example CustomSkinLoader) "
                                 + "and give it this skin file. Only people with the same mod will see it.")
                    }
                    SettingsNote {
                        visible: Skins.microsoft && !Skins.canUpload
                        kind: "warn"
                        text: qsTr("This Microsoft account can't change its skin right now. Sign in again in Accounts, and make sure it owns Minecraft.")
                    }

                    RowLayout {
                        visible: !!page.sel && Skins.canUpload
                        Layout.fillWidth: true
                        spacing: Style.px(8)
                        PrimaryButton {
                            Layout.fillWidth: true
                            iconName: page.sel && page.sel.inUse ? "check" : "user"
                            text: Skins.busy ? Skins.busyText
                                 : page.sel && page.sel.inUse && (page.sel.capeId || "") === Skins.currentCape ? qsTr("You're wearing this")
                                 : page.sel && page.sel.inUse ? qsTr("Update cape")
                                 : qsTr("Use this skin")
                            progress: Skins.busy ? -1 : undefined
                            enabled: !Skins.busy && !(page.sel && page.sel.inUse && (page.sel.capeId || "") === Skins.currentCape)
                            onClicked: Skins.use(page.sel.key)
                        }
                        QuietButton {
                            iconName: "more"
                            tip: qsTr("More")
                            enabled: !Skins.busy
                            onClicked: accountMenu.popup()
                            ThemedMenu {
                                id: accountMenu
                                ThemedMenuItem { text: qsTr("Go back to the default skin"); iconName: "refresh"; onTriggered: Skins.resetToDefault() }
                            }
                        }
                    }
                }
            }

            // Library
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radius + Style.px(4)
                color: Style.panel(0)
                border.width: 1
                border.color: Style.panelBorder

                ColumnLayout {
                    anchors { fill: parent; margins: Style.px(18) }
                    spacing: Style.px(12)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(8)
                        Field {
                            id: playerField
                            Layout.fillWidth: true
                            iconName: "search"
                            placeholderText: qsTr("Player name, to copy their skin")
                            maximumLength: 16
                            enabled: !Skins.busy
                            onAccepted: getBtn.clicked()
                        }
                        QuietButton {
                            id: getBtn
                            iconName: "globe"
                            text: Skins.busy ? Skins.busyText : qsTr("Get skin")
                            enabled: !Skins.busy && playerField.text.trim().length > 0
                            onClicked: { Skins.addFromPlayer(playerField.text); playerField.text = "" }
                        }
                        QuietButton { iconName: "plus"; text: qsTr("Add file"); onClicked: fileDialog.open() }
                        QuietButton { iconName: "folder"; tip: qsTr("Open skins folder"); onClicked: Skins.openFolder() }
                    }

                    GridView {
                        id: grid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: page.skins
                        readonly property int columns: Math.max(2, Math.floor(width / Style.px(150)))
                        cellWidth: Math.floor(width / columns)
                        cellHeight: Style.px(200)
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Item {
                            id: cell
                            required property var modelData
                            readonly property bool chosen: page.sel && page.sel.key === modelData.key
                            property bool renaming: false
                            width: grid.cellWidth
                            height: grid.cellHeight

                            Rectangle {
                                id: card
                                anchors { fill: parent; margins: Style.px(5) }
                                radius: Style.radius
                                color: cell.chosen ? Style.alpha(Style.accent, 0.14) : hover.hovered ? Style.panel(1) : Style.alpha(Style.text, 0.03)
                                border.width: cell.chosen ? 2 : 1
                                border.color: cell.chosen ? Style.accent : Style.panelBorder
                                HoverHandler { id: hover }
                                TapHandler { onTapped: page.selectedKey = cell.modelData.key }

                                SkinFront {
                                    anchors { top: parent.top; topMargin: Style.px(14); horizontalCenter: parent.horizontalCenter }
                                    width: parent.width - Style.px(20)
                                    height: parent.height - Style.px(62)
                                    source: cell.modelData.path
                                    slim: cell.modelData.slim
                                }

                                Rectangle {
                                    visible: cell.modelData.inUse
                                    anchors { top: parent.top; left: parent.left; margins: Style.px(8) }
                                    radius: height / 2
                                    color: Style.accent
                                    implicitWidth: wearing.implicitWidth + Style.px(14)
                                    implicitHeight: wearing.implicitHeight + Style.px(4)
                                    Txt { id: wearing; anchors.centerIn: parent; text: qsTr("Wearing"); color: Style.onAccent; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                                }

                                RowLayout {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Style.px(8) }
                                    spacing: Style.px(4)
                                    Txt {
                                        visible: !cell.renaming
                                        Layout.fillWidth: true
                                        Layout.leftMargin: Style.px(4)
                                        text: cell.modelData.name
                                        font.weight: Font.Medium
                                    }
                                    Field {
                                        id: renameField
                                        visible: cell.renaming
                                        Layout.fillWidth: true
                                        implicitHeight: Style.px(32)
                                        text: cell.modelData.name
                                        onAccepted: { Skins.rename(cell.modelData.key, text); cell.renaming = false }
                                        Keys.onEscapePressed: cell.renaming = false
                                        onActiveFocusChanged: if (!activeFocus) cell.renaming = false
                                    }
                                    QuietButton {
                                        visible: !cell.renaming
                                        plain: true
                                        implicitHeight: Style.px(30)
                                        iconName: "more"
                                        tip: qsTr("More actions")
                                        onClicked: skinMenu.popup()
                                        ThemedMenu {
                                            id: skinMenu
                                            ThemedMenuItem { text: qsTr("Rename"); iconName: "pencil"; onTriggered: { cell.renaming = true; renameField.forceActiveFocus(); renameField.selectAll() } }
                                            ThemedMenuItem { text: qsTr("Delete"); iconName: "trash"; danger: true; enabled: !cell.modelData.inUse; onTriggered: Skins.remove(cell.modelData.key) }
                                        }
                                    }
                                }
                            }
                        }

                        Txt {
                            anchors.centerIn: parent
                            visible: grid.count === 0
                            width: parent.width * 0.8
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            muted: true
                            text: qsTr("Your skin library is empty. Drop PNG skins anywhere on this page, add a file, or type a player name above.")
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose skin images")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Skin images (*.png)")]
        onAccepted: Skins.addFiles(selectedFiles)
    }

    // Drop PNGs anywhere on the page
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (d) => { if (d.hasUrls) { Skins.addFiles(d.urls); d.accept() } }
        Rectangle {
            anchors { fill: parent; margins: Style.px(16) }
            visible: drop.containsDrag
            radius: Style.radius + Style.px(6)
            color: Style.alpha(Style.accent, 0.12)
            border.width: 2
            border.color: Style.accent
            Txt { anchors.centerIn: parent; text: qsTr("Drop to add to your skins"); font.pixelSize: Style.fontTitle; font.weight: Font.DemiBold }
        }
    }
}
