import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Export an instance: a plain zip (to import in this launcher), or a Modrinth / CurseForge modpack.
ModalCard {
    id: dlg
    property string instanceId
    property string format: "zip"
    property var defaults: ({})
    property var entries: []
    property var excluded: ({})
    property int excludedRev: 0
    width: Math.min(parent ? parent.width - Style.px(48) : 620, Style.px(620))
    height: Math.min(parent ? parent.height - Style.px(48) : 680, Style.px(680))
    title: qsTr("Export instance")

    function openFor(id) {
        instanceId = id
        load("zip")
        open()
    }
    function load(fmt) {
        format = fmt
        defaults = InstanceActions.exportDefaults(instanceId, fmt)
        entries = InstanceActions.exportEntries(instanceId, fmt)
        const ex = {}
        for (const e of entries) if (!e.included) ex[e.path] = true
        excluded = ex
        excludedRev++
        packName.text = defaults.name || ""
        packVersion.text = defaults.version || ""
        packAuthor.text = defaults.author || ""
        packSummary.text = defaults.summary || ""
        optional.checked = !!defaults.optionalFiles
        ram.value = defaults.recommendedRam > 0 ? defaults.recommendedRam : 4096
        ramOn.checked = defaults.recommendedRam > 0
    }
    function startExport(fileUrl) {
        const ex = Object.keys(excluded).filter(k => excluded[k])
        const opts = { name: packName.text, version: packVersion.text, author: packAuthor.text, summary: packSummary.text,
                       optionalFiles: optional.checked, recommendedRam: ramOn.checked ? ram.value : 0 }
        if (InstanceActions.exportTo(instanceId, format, fileUrl, opts, ex)) close()
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        title: qsTr("Export to")
        nameFilters: dlg.format === "modrinth" ? [qsTr("Modrinth pack (*.mrpack)")] : [qsTr("Zip archive (*.zip)")]
        onAccepted: dlg.startExport(selectedFile.toString())
    }

    CreateSegmented {
        options: [
            { value: "zip", label: qsTr("Zip") },
            { value: "modrinth", label: qsTr("Modrinth") },
            { value: "curseforge", label: qsTr("CurseForge") }
        ]
        value: dlg.format
        onPicked: (v) => dlg.load(v)
    }
    Txt {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        muted: true
        text: dlg.format === "zip" ? qsTr("The whole instance in one file. Import it in this or any Prism-based launcher.")
            : dlg.format === "modrinth" ? qsTr("A .mrpack modpack: mods from Modrinth are linked, not copied, so the file stays small.")
            : qsTr("A CurseForge modpack zip: mods from CurseForge are linked, the rest is packed in.")
    }
    Txt {
        visible: !!dlg.defaults.problem
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        color: Style.danger
        text: dlg.defaults.problem || ""
    }

    // Pack details
    GridLayout {
        visible: dlg.format !== "zip"
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Style.px(10)
        rowSpacing: Style.px(8)
        Field { id: packName; Layout.fillWidth: true; placeholderText: qsTr("Pack name (instance name)") }
        Field { id: packVersion; Layout.preferredWidth: Style.px(140); placeholderText: qsTr("Version, e.g. 1.0") }
        Field { id: packAuthor; visible: dlg.format === "curseforge"; Layout.columnSpan: 2; Layout.fillWidth: true; placeholderText: qsTr("Author") }
        Field { id: packSummary; visible: dlg.format === "modrinth"; Layout.columnSpan: 2; Layout.fillWidth: true; placeholderText: qsTr("Summary") }
        SettingsToggle {
            id: optional
            Layout.columnSpan: 2
            layoutDirection: Qt.RightToLeft
            label: qsTr("Mark disabled mods as optional")
            onToggled: (on) => checked = on
        }
        RowLayout {
            visible: dlg.format === "curseforge"
            Layout.columnSpan: 2
            spacing: Style.px(10)
            SettingsToggle { id: ramOn; layoutDirection: Qt.RightToLeft; label: qsTr("Recommended memory"); onToggled: (on) => checked = on }
            SpinBox {
                id: ram
                enabled: ramOn.checked
                from: 512; to: 65536; stepSize: 512
                editable: true
                textFromValue: (v) => qsTr("%1 MB").arg(v)
                valueFromText: (t) => parseInt(t)
            }
        }
    }

    // What goes in
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Style.px(4)
        Txt { text: qsTr("Include"); font.weight: Font.DemiBold; Layout.fillWidth: true }
        QuietButton {
            plain: true
            text: qsTr("All")
            onClicked: { dlg.excluded = ({}); dlg.excludedRev++ }
        }
        QuietButton {
            plain: true
            text: qsTr("None")
            onClicked: { const ex = {}; for (const e of dlg.entries) ex[e.path] = true; dlg.excluded = ex; dlg.excludedRev++ }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Style.px(120)
        radius: Style.radius
        color: Style.alpha(Style.bg, 0.35)
        border.width: 1
        border.color: Style.line
        ListView {
            id: files
            anchors { fill: parent; margins: Style.px(6) }
            clip: true
            model: dlg.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: fileRow
                width: files.width
                height: Style.px(34)
                hoverEnabled: true
                readonly property bool on: { dlg.excludedRev; return !dlg.excluded[modelData.path] }
                Accessible.role: Accessible.CheckBox
                Accessible.checked: on
                Accessible.name: modelData.name
                onClicked: { dlg.excluded[modelData.path] = on; dlg.excludedRev++ }
                background: Rectangle { radius: Style.radius - 4; color: fileRow.hovered ? Style.alpha(Style.text, 0.06) : "transparent" }
                contentItem: RowLayout {
                    anchors { fill: parent; leftMargin: Style.px(8); rightMargin: Style.px(8) }
                    spacing: Style.px(10)
                    Rectangle {
                        implicitWidth: Style.px(18); implicitHeight: Style.px(18)
                        radius: 4
                        color: fileRow.on ? Style.accent : "transparent"
                        border.width: fileRow.on ? 0 : 1
                        border.color: Style.textMuted
                        Icon { anchors.centerIn: parent; visible: fileRow.on; name: "check"; size: Style.px(14); color: Style.onAccent }
                    }
                    Icon { name: modelData.isDir ? "folder" : "cube"; size: Style.px(16); color: Style.textMuted }
                    Txt { text: modelData.name; Layout.fillWidth: true; muted: !fileRow.on }
                }
            }
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: Style.px(8)
        QuietButton { text: qsTr("Cancel"); onClicked: dlg.close() }
        PrimaryButton {
            text: qsTr("Export…"); iconName: "export"; implicitHeight: Style.px(40)
            enabled: !dlg.defaults.problem && !InstanceActions.job.active
            onClicked: {
                saveDialog.selectedFile = "file://" + InstanceActions.exportFileName(dlg.instanceId, dlg.format, packName.text)
                saveDialog.open()
            }
        }
    }
}
