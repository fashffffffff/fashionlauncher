import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Create an instance: Minecraft version + mod loader, a modpack, or an import.
// params: { source: "vanilla" | "modpack" | "import", url, group }
Item {
    id: page
    property var params: ({})
    property string source: params.source || "vanilla"
    readonly property var current: source === "modpack" ? modpackTab : source === "import" ? importTab : vanillaTab

    // Details shared by every source
    property bool nameEdited: false
    property var iconChoice: undefined          // undefined = the source's own icon
    readonly property string iconKey: iconChoice !== undefined ? iconChoice : (current.iconSource.length ? "" : "default")
    readonly property string iconPreview: iconKey.length ? "image://instanceicon/" + iconKey : current.iconSource
    readonly property bool canCreate: current.ready && nameField.text.trim().length > 0 && !Creator.busy

    function syncName() { if (!nameEdited) nameField.text = current.suggestedName }
    onCurrentChanged: { Creator.clearError(); iconChoice = undefined; syncName() }
    Connections { target: page.current; function onSuggestedNameChanged() { page.syncName() } }

    function create() {
        if (!canCreate) return
        current.create(nameField.text.trim(), iconKey, groupField.text.trim())
    }

    Component.onCompleted: {
        groupField.text = params.group !== undefined ? params.group : Creator.lastGroup
        if (params.url) importTab.preset(params.url)
        syncName()
        if (Creator.autoTest.length) autoTest.start()
    }

    Connections {
        target: Creator
        function onCreated(id, name) {
            Router.toast(qsTr("“%1” is ready").arg(name), false)
            if (Creator.autoTest.length) { autoQuit.ok = true; autoQuit.start() }
            if (!id) { Router.back(); return }
            Router.selectedId = id
            Router.back()
            Router.openInstance(id)
        }
        function onErrorChanged() { if (Creator.error.length && Creator.autoTest.length) { autoQuit.ok = false; autoQuit.start() } }
    }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(16)

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(16)
            QuietButton {
                plain: true
                iconName: "arrow-left"
                tip: qsTr("Back")
                enabled: !Creator.busy
                onClicked: Router.back()
            }
            Text {
                text: qsTr("Create instance")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(30)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            CreateSegmented {
                enabled: !Creator.busy
                options: [
                    { value: "vanilla", label: qsTr("Minecraft version") },
                    { value: "modpack", label: qsTr("Modpack") },
                    { value: "import", label: qsTr("Import") }
                ]
                value: page.source
                onPicked: v => page.source = v
            }
        }

        // Source
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            enabled: !Creator.busy
            currentIndex: page.source === "modpack" ? 1 : page.source === "import" ? 2 : 0
            CreateVanilla { id: vanillaTab }
            CreateModpack { id: modpackTab }
            CreateImport { id: importTab }
        }

        // Details + create
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: footer.implicitHeight + Style.px(32)
            radius: Style.radius
            color: Style.panel(1)
            border.width: 1
            border.color: Style.panelBorder

            ColumnLayout {
                id: footer
                anchors { fill: parent; margins: Style.px(16) }
                spacing: Style.px(12)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(16)

                    // Icon
                    AbstractButton {
                        id: iconButton
                        enabled: !Creator.busy
                        Layout.preferredWidth: Style.px(64)
                        Layout.preferredHeight: Style.px(64)
                        Layout.alignment: Qt.AlignBottom
                        hoverEnabled: true
                        Accessible.name: qsTr("Change icon")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Change icon")
                        ToolTip.delay: 500
                        onClicked: iconPicker.open()
                        background: Rectangle {
                            radius: Style.radius
                            color: iconButton.hovered ? Style.alpha(Style.text, 0.12) : Style.alpha(Style.text, 0.06)
                            border.width: iconButton.visualFocus ? 2 : 1
                            border.color: iconButton.visualFocus ? Style.accent : Style.panelBorder
                        }
                        contentItem: Item {
                            Image {
                                anchors.centerIn: parent
                                width: Style.px(44); height: width
                                source: page.iconPreview
                                sourceSize: Qt.size(88, 88)
                                fillMode: Image.PreserveAspectFit
                                smooth: page.iconKey.length === 0
                                asynchronous: true
                            }
                            Rectangle {
                                anchors { right: parent.right; bottom: parent.bottom; margins: -Style.px(4) }
                                width: Style.px(22); height: width; radius: width / 2
                                color: Style.surfaceHi
                                border.width: 1
                                border.color: Style.line
                                Icon { anchors.centerIn: parent; name: "pencil"; size: Style.px(12); color: Style.text }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(6)
                        Txt { text: qsTr("Name"); muted: true; font.pixelSize: Style.fontSmall }
                        Field {
                            id: nameField
                            Layout.fillWidth: true
                            enabled: !Creator.busy
                            placeholderText: qsTr("Instance name")
                            Accessible.name: qsTr("Instance name")
                            onTextEdited: page.nameEdited = text.length > 0
                            onAccepted: page.create()
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: Style.px(240)
                        Layout.maximumWidth: Style.px(240)
                        Layout.fillWidth: false
                        spacing: Style.px(6)
                        Txt { text: qsTr("Group (optional)"); muted: true; font.pixelSize: Style.fontSmall }
                        Field {
                            id: groupField
                            Layout.fillWidth: true
                            enabled: !Creator.busy
                            rightPadding: Style.px(40)
                            placeholderText: qsTr("No group")
                            Accessible.name: qsTr("Group")
                            QuietButton {
                                plain: true
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: Style.px(2) }
                                implicitHeight: Style.px(34)
                                iconName: "chevron-right"
                                rotation: 90
                                tip: qsTr("Existing groups")
                                onClicked: { groupMenu.groups = Creator.groups(); groupMenu.popup(groupField, 0, groupField.height) }
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: Style.px(6)
                        Layout.alignment: Qt.AlignBottom
                        RowLayout {
                            spacing: Style.px(8)
                            QuietButton {
                                visible: Creator.busy && Creator.canCancel
                                text: qsTr("Cancel")
                                onClicked: Creator.cancel()
                            }
                            PrimaryButton {
                                id: createButton
                                text: Creator.busy ? qsTr("Creating…") : qsTr("Create")
                                iconName: Creator.busy ? "" : "plus"
                                enabled: page.canCreate
                                opacity: enabled || Creator.busy ? 1 : 0.5
                                progress: Creator.busy ? Creator.progress : undefined
                                implicitHeight: Style.px(44)
                                onClicked: page.create()
                            }
                        }
                    }
                }

                // Status line: what's missing, progress, or what went wrong
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(8)
                    visible: statusText.text.length > 0
                    Icon {
                        visible: Creator.error.length > 0
                        name: "close"
                        size: Style.px(16)
                        color: Style.danger
                        Layout.alignment: Qt.AlignTop
                    }
                    Txt {
                        id: statusText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        maximumLineCount: 4
                        color: Creator.error.length ? Style.danger : Style.textMuted
                        text: Creator.error.length ? Creator.error
                            : Creator.busy ? (Creator.status || qsTr("Working…"))
                                + (Creator.progress >= 0 ? "  ·  " + Math.round(Creator.progress * 100) + "%" : "")
                                + "\n" + qsTr("You can leave this page: the install keeps going and its progress stays at the bottom of the window.")
                            : page.current.blocker
                    }
                    QuietButton { visible: Creator.error.length > 0; plain: true; text: qsTr("Dismiss"); onClicked: Creator.clearError() }
                }
            }
        }
    }

    CreateIconPicker {
        id: iconPicker
        anchors.centerIn: Overlay.overlay
        currentKey: page.iconKey
        autoLabel: page.current.autoIconLabel
        onChosen: key => page.iconChoice = key.length ? key : undefined
    }

    ThemedMenu {
        id: groupMenu
        property var groups: []
        ThemedMenuItem { text: qsTr("No group"); iconName: groupField.text.length ? "" : "check"; onTriggered: groupField.text = "" }
        MenuSeparator { visible: groupMenu.groups.length > 0 }
        Instantiator {
            model: groupMenu.groups
            delegate: ThemedMenuItem {
                text: modelData
                iconName: modelData === groupField.text ? "check" : "folder"
                onTriggered: groupField.text = modelData
            }
            onObjectAdded: (index, object) => groupMenu.insertItem(index + 2, object)
            onObjectRemoved: (index, object) => groupMenu.removeItem(object)
        }
    }

    // Automation (FFPRISM_CREATE=vanilla:<mc> | fabric:<mc> | modrinth:<search> | curseforge:<search> | import:<file or url>)
    Timer {
        id: autoTest
        interval: 600
        repeat: true
        property int phase: 0
        onTriggered: {
            const spec = Creator.autoTest
            const kind = spec.slice(0, spec.indexOf(":")), arg = spec.slice(spec.indexOf(":") + 1)
            if (Creator.busy) return
            if (kind === "vanilla" || ["fabric", "quilt", "forge", "neoforge"].includes(kind)) {
                if (phase === 0) { page.source = "vanilla"; vanillaTab.mcVersion = arg; vanillaTab.loader = kind === "vanilla" ? "" : kind; phase = 1; return }
            } else if (kind === "modrinth" || kind === "curseforge") {
                if (phase === 0) { page.source = "modpack"; Creator.packs.provider = kind; modpackTab.search(arg); phase = 1; return }
                if (phase === 1) {
                    if (Creator.packs.count > 0) { modpackTab.selectedRow = 0; Creator.selectPack(0); phase = 2 }
                    return
                }
            } else if (kind === "import") {
                if (phase === 0) { page.source = "import"; importTab.preset(arg); phase = 1; return }
            }
            if (page.canCreate) { stop(); console.log("[create] auto test creating", nameField.text); page.create() }
        }
    }
    Timer { id: autoQuit; interval: 1500; property bool ok; onTriggered: Creator.finishAutoTest(ok) }
}
