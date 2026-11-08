import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Copy Minecraft's own settings (controls, video, sound…) from one instance to another or to all of them.
// Opened through Router.copyGameOptions({ source, target }) — target is an id, "*" for all, or "".
ModalCard {
    id: dlg
    property string source: ""
    property string target: ""
    property var summary: ({})
    property var picked: ({ controls: true, video: true, sound: true, chat: true, language: true, other: false })
    property var planned: ({})

    width: Math.min(parent ? parent.width - Style.px(48) : 600, Style.px(600))
    title: qsTr("Copy game settings")
    subtitle: qsTr("Minecraft's own settings: controls, video, sound. Mods and worlds stay as they are.")

    readonly property bool hasSource: !!summary.exists
    readonly property var categories: Object.keys(picked).filter(k => picked[k] && count(k) > 0)
    readonly property var targets: !source ? [] : target === "*" ? GameOptions.allOtherInstances(source)
                                                 : (target && target !== source ? [target] : [])

    function openFor(params) {
        source = params.source || ""
        target = params.target || ""
        if (target === source) target = ""
        refresh()
        open()
    }
    function refresh() {
        summary = source ? GameOptions.summary(source) : {}
        replan()
    }
    function replan() { planned = targets.length && categories.length ? GameOptions.plan(source, targets, categories) : {} }
    onTargetsChanged: Qt.callLater(replan)
    onCategoriesChanged: Qt.callLater(replan)

    function cat(k) { return summary.categories ? summary.categories[k] || {} : {} }
    function count(k) { return cat(k).count || 0 }
    function preview(k) {
        const c = cat(k)
        if (!c.count) return qsTr("Nothing saved yet")
        const parts = []
        if (k === "controls") {
            if (c.keybinds) parts.push(qsTr("%n keybind(s)", "", c.keybinds))
            if (c.sensitivity !== undefined) parts.push(qsTr("sensitivity %1%").arg(c.sensitivity))
        } else if (k === "video") {
            if (c.renderDistance !== undefined) parts.push(qsTr("render distance %1").arg(c.renderDistance))
            if (c.guiScale !== undefined) parts.push(c.guiScale === 0 ? qsTr("GUI scale auto") : qsTr("GUI scale %1").arg(c.guiScale))
            if (c.fov !== undefined) parts.push(qsTr("FOV %1").arg(c.fov))
            if (c.shaderPack) parts.push(qsTr("shaders: %1").arg(c.shaderPack.replace(/\.zip$/, "")))
            else if ((c.extras || []).indexOf("optionsof.txt") >= 0) parts.push("OptiFine")
            if ((c.extras || []).indexOf("config/sodium-options.json") >= 0) parts.push("Sodium")
        } else if (k === "sound") {
            if (c.master !== undefined) parts.push(qsTr("master volume %1%").arg(c.master))
            parts.push(qsTr("%n setting(s)", "", c.count))
        } else if (k === "language") {
            if (c.lang) parts.push(c.lang)
            if (c.skinParts) parts.push(qsTr("%n skin part(s) shown", "", c.skinParts))
        }
        if (!parts.length) parts.push(qsTr("%n setting(s)", "", c.count))
        return parts.join(" · ")
    }

    function start() {
        const r = GameOptions.apply(source, targets, categories)
        close()
        if (!r.ok && r.error) { Router.toast(r.error, true); return }
        if (!r.written) {
            Router.toast(r.running && r.running.length ? qsTr("Nothing was changed: the game is running.")
                                                        : qsTr("Nothing to change: the settings are already the same."))
            return
        }
        const msg = r.written === 1 && targets.length === 1
            ? qsTr("Settings copied to “%1”.").arg(nameOf(targets[0]))
            : qsTr("Settings copied to %n instance(s).", "", r.written)
        Router.toastAction(msg, qsTr("Undo"), () => {
            if (GameOptions.undo()) Router.toast(qsTr("Previous settings are back."))
            else Router.toast(qsTr("Some files couldn't be restored. Backups are next to them (.bak)."), true)
        })
    }
    function nameOf(id) {
        const i = Instances.indexOf(id)
        return i >= 0 ? Instances.get(i).name : ""
    }

    // ---- from → to
    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(10)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: Style.px(6)
            Txt { text: qsTr("From"); muted: true; font.pixelSize: Style.fontSmall }
            InstanceChooser {
                Layout.fillWidth: true
                value: dlg.source
                onChosen: (id) => { dlg.source = id; if (dlg.target === id) dlg.target = ""; dlg.refresh() }
            }
        }
        Icon {
            Layout.alignment: Qt.AlignBottom
            Layout.bottomMargin: Style.px(22)
            name: "chevron-right"
            size: Style.px(16)
            color: Style.textMuted
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: Style.px(6)
            Txt { text: qsTr("To"); muted: true; font.pixelSize: Style.fontSmall }
            InstanceChooser {
                Layout.fillWidth: true
                value: dlg.target
                exceptId: dlg.source
                allowAll: true
                onChosen: (id) => dlg.target = id
            }
        }
    }

    // ---- what to copy
    Txt {
        visible: !dlg.hasSource && !!dlg.source
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        muted: true
        text: qsTr("“%1” has no game settings yet. Play it once, change what you like, then copy.").arg(dlg.nameOf(dlg.source))
    }
    ColumnLayout {
        visible: dlg.hasSource
        Layout.fillWidth: true
        spacing: Style.px(2)
        Repeater {
            model: [
                { key: "controls", label: qsTr("Controls"), icon: "cube" },
                { key: "video", label: qsTr("Video"), icon: "image" },
                { key: "sound", label: qsTr("Sound"), icon: "play" },
                { key: "chat", label: qsTr("Chat and accessibility"), icon: "user" },
                { key: "language", label: qsTr("Language and skin"), icon: "globe" },
                { key: "other", label: qsTr("Other"), icon: "more" }
            ]
            delegate: AbstractButton {
                id: catRow
                required property var modelData
                readonly property bool available: dlg.count(modelData.key) > 0
                readonly property bool on: available && !!dlg.picked[modelData.key]
                Layout.fillWidth: true
                implicitHeight: Style.px(52)
                enabled: available
                hoverEnabled: true
                Accessible.name: modelData.label
                Accessible.checkable: true
                Accessible.checked: on
                function flip() { const p = Object.assign({}, dlg.picked); p[modelData.key] = !p[modelData.key]; dlg.picked = p }
                onClicked: { Sounds.play("toggle"); flip() }
                background: Rectangle {
                    radius: Style.radius - 4
                    color: catRow.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                    border.width: catRow.visualFocus ? 2 : 0
                    border.color: Style.accent
                }
                contentItem: RowLayout {
                    spacing: Style.px(12)
                    opacity: catRow.available ? 1 : 0.45
                    // a small block-style check box
                    Rectangle {
                        Layout.leftMargin: Style.px(8)
                        Layout.preferredWidth: Style.px(20); Layout.preferredHeight: Style.px(20)
                        radius: Style.px(4)
                        color: catRow.on ? Style.accent : "transparent"
                        border.width: catRow.on ? 0 : 2
                        border.color: Style.alpha(Style.text, 0.35)
                        Icon { anchors.centerIn: parent; visible: catRow.on; name: "check"; size: Style.px(14); color: Style.inkOn(Style.accent, Style.onAccent) }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(1)
                        Txt { text: catRow.modelData.label; font.weight: Font.Medium; Layout.fillWidth: true }
                        Txt { text: dlg.preview(catRow.modelData.key); muted: true; font.pixelSize: Style.fontSmall; Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    // ---- what will happen
    Txt {
        id: note
        Layout.fillWidth: true
        visible: text.length > 0
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        muted: true
        font.pixelSize: Style.fontSmall
        text: {
            const p = dlg.planned
            const lines = []
            if (p.converted) lines.push(qsTr("Key bindings will be converted between the old (1.12 and earlier) and new format."))
            if (p.skipped) lines.push(qsTr("%n key binding(s) have no match in the other version and will be skipped.", "", p.skipped))
            if (p.running && p.running.length) lines.push(qsTr("Running now, won't be changed: %1").arg(p.running.join(", ")))
            if (dlg.targets.length && dlg.hasSource) lines.push(qsTr("The current files are backed up first, and you can undo right after copying."))
            return lines.join("\n")
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        Layout.topMargin: Style.px(4)
        spacing: Style.px(8)
        QuietButton { text: qsTr("Cancel"); onClicked: dlg.close() }
        PrimaryButton {
            text: qsTr("Copy settings"); iconName: "copy"; implicitHeight: Style.px(40)
            enabled: dlg.hasSource && dlg.targets.length > 0 && dlg.categories.length > 0
            opacity: enabled ? 1 : 0.5
            onClicked: dlg.start()
        }
    }
}
