import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// "Import" source: a modpack or exported instance from a file or a link.
Item {
    id: tab
    property string source: ""
    readonly property string detected: Creator.importName(source)
    readonly property bool isCurseLink: source.trim().startsWith("curseforge://")

    readonly property string suggestedName: detected
    readonly property bool ready: detected.length > 0 || isCurseLink
    readonly property string iconSource: ""
    readonly property string autoIconLabel: ""
    readonly property string blocker: source.trim().length === 0 ? qsTr("Choose a file or paste a link.")
                                    : ready ? "" : qsTr("That isn't a .zip or .mrpack file or a download link.")
    function create(name, icon, group) { return Creator.createImport(source, name, icon, group) }

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
            hint: qsTr("Modrinth packs (.mrpack), CurseForge packs and instances exported from this or other launchers (.zip).")
            Layout.fillWidth: true
        }

        // Drop zone
        Rectangle {
            id: zone
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

        Txt { text: qsTr("Or paste a link"); font.weight: Font.Medium; Layout.topMargin: Style.px(4) }
        Field {
            id: linkField
            Layout.fillWidth: true
            iconName: "link"
            placeholderText: qsTr("https://… or curseforge://…")
            Accessible.name: qsTr("Link to a modpack")
            onTextEdited: tab.source = text
        }
        Item { Layout.fillHeight: true }
    }

    // Opened with a link or file from outside (e.g. a drop on the window)
    function preset(url) { if (url) { source = url; if (!url.startsWith("file:")) linkField.text = url } }
}
