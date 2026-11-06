import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// "Import" source: a modpack or exported instance from a file or a link, or instances of another launcher.
Item {
    id: tab
    property string mode: "file"            // "file" | "launchers"
    property string source: ""
    readonly property string detected: Creator.importName(source)
    readonly property bool isCurseLink: source.trim().startsWith("curseforge://")

    // From another launcher
    property var picked: ({})               // key -> true
    property bool linkFiles: false
    readonly property var pickedKeys: Object.keys(picked).filter(k => picked[k])
    readonly property var pickedEntries: Tools.launchers.filter(e => picked[e.key])

    readonly property string suggestedName: mode === "launchers"
        ? (pickedEntries.length === 1 ? pickedEntries[0].name : pickedEntries.length > 1 ? qsTr("%n instance(s)", "", pickedEntries.length) : "")
        : detected
    readonly property bool ready: mode === "launchers" ? pickedEntries.length > 0 : (detected.length > 0 || isCurseLink)
    readonly property string iconSource: ""
    readonly property string autoIconLabel: ""
    readonly property string blocker: mode === "launchers"
        ? (Tools.scanning ? qsTr("Looking for other launchers…") : ready ? (pickedEntries.length > 1 ? qsTr("Each instance keeps its own name.") : "") : qsTr("Pick the instances to bring over."))
        : source.trim().length === 0 ? qsTr("Choose a file or paste a link.")
        : ready ? "" : qsTr("That isn't a .zip or .mrpack file or a download link.")
    function create(name, icon, group) {
        if (mode === "launchers") return Creator.createFromLaunchers(pickedKeys, linkFiles, name, icon, group)
        return Creator.createImport(source, name, icon, group)
    }
    function setPicked(key, on) { const p = Object.assign({}, picked); p[key] = on; picked = p }
    onModeChanged: if (mode === "launchers" && !Tools.scanning && Tools.launchers.length === 0 && !scannedOnce) { scannedOnce = true; Tools.refreshLaunchers() }
    property bool scannedOnce: false
    Component.onCompleted: if (Tools.automation === "launchers" || Tools.automation.startsWith("folder:") || Tools.automation.startsWith("import:")) { scannedOnce = true; mode = "launchers"; autoImport() }
    Connections {
        target: Tools
        function onLaunchersChanged() {
            tab.picked = ({})
            tab.autoImport()
        }
    }
    // automation (FFPRISM_TOOLS=import:<folder>): import everything that was found
    property bool autoImported: false
    function autoImport() {
        if (autoImported || !Tools.automation.startsWith("import:") || Tools.scanning || !Tools.launchers.length) return
        autoImported = true
        console.log("[tools] auto import", Tools.launchers.length)
        Creator.createFromLaunchers(Tools.launchers.filter(e => e.importable).map(e => e.key), false, "", "default", "")
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose a launcher or instance folder")
        onAccepted: Tools.scanFolder(selectedFolder.toString())
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose a modpack file")
        nameFilters: [qsTr("Modpacks (*.zip *.mrpack)"), qsTr("All files (*)")]
        onAccepted: { tab.source = selectedFile.toString(); linkField.text = "" }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(14)

        CreateStep {
            number: 1
            title: qsTr("Choose what to import")
            hint: tab.mode === "launchers" ? qsTr("Instances from CurseForge, Modrinth App, ATLauncher, MultiMC, Prism and the official Minecraft launcher on this computer.")
                                           : qsTr("Modrinth packs (.mrpack), CurseForge packs and instances exported from this or other launchers (.zip).")
            Layout.fillWidth: true
        }
        CreateSegmented {
            enabled: !Creator.busy
            options: [{ value: "file", label: qsTr("File or link") }, { value: "launchers", label: qsTr("From another launcher") }]
            value: tab.mode
            onPicked: (v) => tab.mode = v
        }

        // Drop zone
        Rectangle {
            id: zone
            visible: tab.mode === "file"
            Layout.fillWidth: true
            Layout.preferredHeight: Style.px(300)
            radius: Style.radius
            color: drop.containsDrag ? Style.alpha(Style.accent, 0.14) : Style.panel(0)
            border.width: drop.containsDrag ? 2 : 1
            border.color: drop.containsDrag ? Style.accent : Style.panelBorder
            Behavior on color { ColorAnimation { duration: Style.dur(120) } }

            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: d => {
                    if (d.hasUrls && d.urls.length > 0) { tab.source = d.urls[0].toString(); linkField.text = ""; d.accept() }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - Style.px(48)
                spacing: Style.px(12)
                Icon { name: "export"; size: Style.px(36); color: Style.textMuted; Layout.alignment: Qt.AlignHCenter; rotation: 180 }
                Text {
                    text: tab.detected.length && !linkField.text.length ? tab.detected : qsTr("Drop a file here")
                    color: Style.text
                    font.family: Style.ui
                    font.pixelSize: Style.fontLead
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Txt {
                    text: tab.detected.length && !linkField.text.length ? qsTr("Ready to import.") : qsTr("or")
                    muted: true
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                QuietButton {
                    text: tab.detected.length && !linkField.text.length ? qsTr("Choose another file…") : qsTr("Choose file…")
                    iconName: "folder"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: fileDialog.open()
                }
            }
        }

        Txt { visible: tab.mode === "file"; text: qsTr("Or paste a link"); font.weight: Font.Medium; Layout.topMargin: Style.px(4) }
        Field {
            id: linkField
            visible: tab.mode === "file"
            Layout.fillWidth: true
            iconName: "link"
            placeholderText: qsTr("https://… or curseforge://…")
            Accessible.name: qsTr("Link to a modpack")
            onTextEdited: tab.source = text
        }
        Item { visible: tab.mode === "file"; Layout.fillHeight: true }

        ToolsLauncherList {
            visible: tab.mode === "launchers"
            Layout.fillWidth: true
            Layout.fillHeight: true
            picked: tab.picked
            linkFiles: tab.linkFiles
            onToggled: (key, on) => tab.setPicked(key, on)
            onLinkToggled: (on) => tab.linkFiles = on
            onChooseFolder: folderDialog.open()
        }
    }

    // Opened with a link or file from outside (e.g. a drop on the window)
    function preset(url) { if (url) { mode = "file"; source = url; if (!url.startsWith("file:")) linkField.text = url } }
}
