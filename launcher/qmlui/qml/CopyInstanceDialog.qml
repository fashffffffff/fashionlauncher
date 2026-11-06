import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Copy an instance: new name, group, and what to bring along.
ModalCard {
    id: dlg
    property string instanceId
    property string iconKey: ""
    width: Math.min(parent ? parent.width - Style.px(48) : 560, Style.px(560))
    title: qsTr("Copy instance")

    function openFor(id) {
        instanceId = id
        const d = InstanceActions.copyDefaults(id)
        nameField.text = qsTr("%1 (copy)").arg(d.name || "")
        groupBox.model = [""].concat(InstanceActions.groups())
        groupBox.editText = d.group || ""
        iconKey = d.icon || "default"
        for (const k in toggles.opts) toggles.opts[k] = true
        toggles.opts.links = false
        toggles.optsChanged()
        open()
    }
    function start() {
        const opts = Object.assign({ name: nameField.text, group: groupBox.editText, icon: iconKey }, toggles.opts)
        if (InstanceActions.copy(instanceId, opts)) close()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(12)
        AbstractButton {
            id: iconBtn
            implicitWidth: Style.px(56); implicitHeight: Style.px(56)
            padding: Style.px(8)
            hoverEnabled: true
            Accessible.name: qsTr("Icon")
            ToolTip.visible: hovered; ToolTip.text: qsTr("Change icon"); ToolTip.delay: 500
            onClicked: iconPicker.open()
            background: Rectangle {
                radius: Style.radius
                color: iconBtn.hovered ? Style.surfaceHi : Style.alpha(Style.surfaceHi, 0.5)
                border.width: iconBtn.visualFocus ? 2 : 1
                border.color: iconBtn.visualFocus ? Style.accent : Style.line
            }
            contentItem: Image {
                source: "image://instanceicon/" + dlg.iconKey
                sourceSize: Qt.size(96, 96)
                fillMode: Image.PreserveAspectFit
                smooth: false
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.px(6)
            Field { id: nameField; Layout.fillWidth: true; placeholderText: qsTr("Name"); onAccepted: dlg.start() }
            ComboBox {
                id: groupBox
                Layout.fillWidth: true
                editable: true
                implicitHeight: Style.px(36)
                font.family: Style.ui
                font.pixelSize: Style.fontBody
                displayText: editText.length ? editText : qsTr("No group")
                background: Rectangle { radius: Style.radius; color: Style.alpha(Style.surface, 0.85); border.width: 1; border.color: Style.line }
                contentItem: TextField {
                    text: groupBox.editText
                    onTextEdited: groupBox.editText = text
                    placeholderText: qsTr("No group")
                    placeholderTextColor: Style.textMuted
                    color: Style.text
                    font: groupBox.font
                    leftPadding: Style.px(12)
                    verticalAlignment: TextInput.AlignVCenter
                    background: null
                }
            }
        }
    }

    Txt { text: qsTr("Bring along"); font.weight: Font.DemiBold; Layout.topMargin: Style.px(4) }
    GridLayout {
        id: toggles
        // Keys match InstanceActions.copy options
        property var opts: ({ saves: true, keepPlaytime: true, options: true, mods: true, resourcePacks: true,
                               shaderPacks: true, servers: true, screenshots: true, links: false })
        signal optsTouched()
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Style.px(24)
        rowSpacing: Style.px(8)
        Repeater {
            model: [
                { key: "saves", label: qsTr("Worlds") },
                { key: "mods", label: qsTr("Mods") },
                { key: "resourcePacks", label: qsTr("Resource packs") },
                { key: "shaderPacks", label: qsTr("Shaders") },
                { key: "options", label: qsTr("Game options") },
                { key: "servers", label: qsTr("Server list") },
                { key: "screenshots", label: qsTr("Screenshots") },
                { key: "keepPlaytime", label: qsTr("Play time") }
            ]
            delegate: SettingsToggle {
                Layout.fillWidth: true
                layoutDirection: Qt.RightToLeft
                label: modelData.label
                checked: { toggles.optsChanged; return toggles.opts[modelData.key] }
                onToggled: (on) => { toggles.opts[modelData.key] = on; toggles.optsChanged() }
            }
        }
    }
    SettingsToggle {
        layoutDirection: Qt.RightToLeft
        label: qsTr("Share files with the original instead of copying (saves disk space)")
        checked: { toggles.optsChanged; return toggles.opts.links }
        onToggled: (on) => { toggles.opts.links = on; toggles.optsChanged() }
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        Layout.topMargin: Style.px(6)
        spacing: Style.px(8)
        QuietButton { text: qsTr("Cancel"); onClicked: dlg.close() }
        PrimaryButton {
            text: qsTr("Copy"); iconName: "copy"; implicitHeight: Style.px(40)
            enabled: nameField.text.trim().length > 0 && !InstanceActions.job.active
            onClicked: dlg.start()
        }
    }

    CreateIconPicker {
        id: iconPicker
        parent: Overlay.overlay
        anchors.centerIn: parent
        currentKey: dlg.iconKey
        onChosen: (key) => { if (key) dlg.iconKey = key }
    }
}
