import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings → Storage: where disk space goes, and what the launcher can safely clear. The scan and the cleanup run
// as jobs in the task indicator; StorageScan.h has the rules for what counts as removable.
ColumnLayout {
    id: st
    spacing: Style.px(16)

    readonly property var report: Storage.report
    readonly property bool hasReport: report && report.categories !== undefined
    property int logDays: 30
    property bool screenshots: false
    // category id -> picked; screenshots are never picked on their own
    property var picked: ({ libraries: true, assets: true, cache: true, logs: true, leftovers: true,
                            duplicates: true, snapshots: true, screenshots: false })
    property string expanded: ""

    function size(b) {
        if (b >= 1e9) return qsTr("%1 GB").arg((b / 1e9).toFixed(1))
        if (b >= 1e6) return qsTr("%1 MB").arg(Math.round(b / 1e6))
        if (b >= 1e3) return qsTr("%1 KB").arg(Math.round(b / 1e3))
        return qsTr("%1 B").arg(Math.round(b))
    }
    function rescan() { Storage.scan(logDays, screenshots) }

    readonly property var usageInfo: ({
        worlds: { label: qsTr("Worlds"), color: Style.success },
        mods: { label: qsTr("Mods and packs"), color: Style.accent },
        game: { label: qsTr("Game files"), color: Style.mix(Style.accent, Style.bg, 0.5) },
        java: { label: qsTr("Java"), color: hue(0.2) },
        screenshots: { label: qsTr("Screenshots"), color: hue(0.5) },
        logs: { label: qsTr("Logs and cache"), color: hue(0.33) },
        other: { label: qsTr("Other"), color: Style.alpha(Style.text, 0.28) }
    })
    // Flat colours that sit with the theme: the accent turned around the colour wheel.
    function hue(shift) {
        const a = Style.accent
        return Qt.hsla((a.hslHue + shift) % 1, Math.min(0.75, a.hslSaturation * 0.85), Style.dark ? 0.62 : 0.48, 1)
    }

    function info(id) {
        switch (id) {
        case "libraries": return { title: qsTr("Unused game libraries"),
            text: qsTr("Minecraft versions, loaders and libraries that no instance uses. They are downloaded again if an instance needs them.") }
        case "assets": return { title: qsTr("Unused game assets"),
            text: qsTr("Sounds and translations of Minecraft versions that no instance uses.") }
        case "cache": return { title: qsTr("Download cache"),
            text: qsTr("Modpack archives, icons and other downloads kept to speed things up.") }
        case "logs": return { title: qsTr("Old logs and crash reports"),
            text: qsTr("Older than %n day(s), from every instance and the launcher.", "", st.logDays) }
        case "leftovers": return { title: qsTr("Leftover files"),
            text: qsTr("Backups (.bak) and unfinished temporary files inside instances.") }
        case "duplicates": return { title: qsTr("Identical mods in several instances"),
            text: qsTr("Kept once on disk through the shared store. Nothing is deleted.") }
        case "snapshots": return { title: qsTr("Old snapshots"),
            text: qsTr("Snapshots beyond the last 5 of each instance, and stored files nothing needs any more.") }
        case "screenshots": return { title: qsTr("Screenshots"),
            text: qsTr("Every screenshot of every instance. They can't be brought back.") }
        }
        return { title: id, text: "" }
    }
    function noteText(note) {
        if (note.startsWith("profiles:"))
            return qsTr("Can't check: the game version of “%1” isn't fully downloaded. Start it once, then scan again.").arg(note.slice(9))
        if (note.startsWith("assetIndex:"))
            return qsTr("Can't check: the game asset list “%1” isn't downloaded. Start the instances that use it once, then scan again.").arg(note.slice(11))
        return note
    }

    readonly property var chosen: {
        if (!hasReport) return []
        return report.categories.filter(c => c.available && c.count > 0 && picked[c.id] === true)
    }
    readonly property real chosenBytes: chosen.reduce((s, c) => s + c.bytes, 0)

    Component.onCompleted: if (!hasReport && !Storage.scanning) rescan()

    Connections {
        target: Storage
        function onCleaned(freed, failed) {
            Sounds.play("success")
            Router.toast(failed > 0 ? qsTr("Freed %1. %n file(s) couldn't be removed.", "", failed).arg(st.size(freed))
                                    : qsTr("Freed %1").arg(st.size(freed)), false)
        }
    }

    // ---- Where the space goes ----
    SettingsSection {
        title: qsTr("Disk space")
        description: qsTr("What %1 keeps on this computer. Worlds are never removed here.").arg(Launcher.appName)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            Text {
                text: st.hasReport ? st.size(st.report.total) : "—"
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(30)
            }
            Txt {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBaseline
                text: Storage.scanning ? qsTr("Measuring…") : qsTr("in total")
                muted: true
            }
            QuietButton {
                iconName: "refresh"
                tip: qsTr("Measure again")
                enabled: !Storage.scanning && !Storage.cleaning
                onClicked: st.rescan()
            }
        }

        // stacked bar
        Item {
            Layout.fillWidth: true
            implicitHeight: Style.px(16)
            Rectangle {
                anchors.fill: parent
                radius: Style.px(4)
                color: Style.alpha(Style.text, 0.08)
            }
            Row {
                id: bar
                anchors.fill: parent
                spacing: Style.px(2)
                readonly property var parts: st.hasReport ? st.report.usage.filter(u => u.bytes > 0) : []
                readonly property real usable: width - spacing * Math.max(0, parts.length - 1)
                Repeater {
                    model: bar.parts
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        height: bar.height
                        width: Math.max(Style.px(3), bar.usable * modelData.bytes / Math.max(1, st.report.total))
                        color: st.usageInfo[modelData.id].color
                        radius: index === 0 || index === bar.parts.length - 1 ? Style.px(4) : 0
                        // square the inner corners of the end pieces
                        Rectangle {
                            visible: bar.parts.length > 1 && (parent.index === 0 || parent.index === bar.parts.length - 1)
                            width: parent.radius; height: parent.height; color: parent.color
                            x: parent.index === 0 ? parent.width - width : 0
                        }
                        HoverHandler { id: segHover }
                        ToolTip.visible: segHover.hovered
                        ToolTip.text: st.usageInfo[modelData.id].label + " — " + st.size(modelData.bytes)
                    }
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Style.px(18)
            Repeater {
                model: st.hasReport ? st.report.usage.filter(u => u.bytes > 0) : []
                delegate: Row {
                    required property var modelData
                    spacing: Style.px(8)
                    Rectangle {
                        width: Style.px(10); height: width; radius: Style.px(2)
                        color: st.usageInfo[modelData.id].color
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Txt { text: st.usageInfo[modelData.id].label; font.pixelSize: Style.fontSmall }
                    Txt { text: st.size(modelData.bytes); muted: true; font.pixelSize: Style.fontSmall }
                }
            }
        }
    }

    // ---- What can go ----
    SettingsSection {
        title: qsTr("Free up space")
        description: qsTr("Pick what to clear. Open a line to see exactly which files go.")

        SettingsRow {
            label: qsTr("Logs count as old after")
            SettingsSegmented {
                options: [{ value: 7, label: qsTr("%n day(s)", "", 7) }, { value: 30, label: qsTr("%n day(s)", "", 30) },
                          { value: 90, label: qsTr("%n day(s)", "", 90) }]
                value: st.logDays
                onPicked: (v) => { st.logDays = v; st.rescan() }
            }
        }
        SettingsSwitch {
            label: qsTr("Also offer screenshots")
            description: qsTr("Off unless you turn it on. Screenshots are yours and can't be brought back.")
            checked: st.screenshots
            onToggled: (on) => { st.screenshots = on; st.rescan() }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Style.alpha(Style.text, 0.08) }

        Txt {
            visible: !st.hasReport
            text: qsTr("Measuring…")
            muted: true
        }

        Repeater {
            model: st.hasReport ? st.report.categories : []
            delegate: ColumnLayout {
                id: cat
                required property var modelData
                readonly property var meta: st.info(modelData.id)
                readonly property bool usable: modelData.available && modelData.count > 0
                readonly property bool on: usable && st.picked[modelData.id] === true
                readonly property bool open: st.expanded === modelData.id
                Layout.fillWidth: true
                spacing: Style.px(6)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(12)
                    AbstractButton {
                        id: box
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: Style.px(2)
                        implicitWidth: Style.px(20); implicitHeight: Style.px(20)
                        enabled: cat.usable
                        focusPolicy: Qt.StrongFocus
                        Accessible.role: Accessible.CheckBox
                        Accessible.checked: cat.on
                        Accessible.name: cat.meta.title
                        onClicked: {
                            const p = Object.assign({}, st.picked)
                            p[cat.modelData.id] = !cat.on
                            st.picked = p
                        }
                        background: Rectangle {
                            radius: Style.px(5)
                            color: cat.on ? Style.accent : "transparent"
                            border.width: cat.on ? 0 : 2
                            border.color: box.visualFocus ? Style.accent : Style.alpha(Style.text, cat.usable ? 0.35 : 0.15)
                            Icon { anchors.centerIn: parent; visible: cat.on; name: "check"; size: Style.px(14); color: Style.onAccent }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(2)
                        opacity: cat.usable ? 1 : 0.6
                        Txt { text: cat.meta.title; Layout.fillWidth: true; font.weight: Font.DemiBold }
                        Txt {
                            Layout.fillWidth: true
                            text: cat.modelData.available ? cat.meta.text : st.noteText(cat.modelData.note)
                            muted: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Style.fontSmall
                        }
                        AbstractButton {
                            id: more
                            visible: cat.usable
                            implicitHeight: Style.px(24)
                            implicitWidth: moreRow.implicitWidth
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            onClicked: st.expanded = cat.open ? "" : cat.modelData.id
                            contentItem: Row {
                                id: moreRow
                                spacing: Style.px(4)
                                Icon {
                                    name: "chevron-right"; size: Style.px(14); color: Style.accent
                                    rotation: cat.open ? 90 : 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    Behavior on rotation { NumberAnimation { duration: Style.dur(120) } }
                                }
                                Txt {
                                    text: cat.open ? qsTr("Hide files") : qsTr("Show %n file(s)", "", cat.modelData.count)
                                    color: Style.accent
                                    font.pixelSize: Style.fontSmall
                                    font.underline: more.hovered
                                }
                            }
                        }
                    }
                    Txt {
                        Layout.alignment: Qt.AlignTop
                        text: cat.modelData.available ? (cat.modelData.count > 0 ? st.size(cat.modelData.bytes) : qsTr("Nothing")) : ""
                        muted: !cat.on
                        font.weight: cat.on ? Font.DemiBold : Font.Normal
                    }
                }

                // file preview
                Rectangle {
                    visible: cat.open
                    Layout.fillWidth: true
                    Layout.leftMargin: Style.px(32)
                    implicitHeight: Math.min(Style.px(260), list.contentHeight + Style.px(12))
                    radius: Style.radius
                    color: Style.alpha(Style.text, 0.05)
                    ListView {
                        id: list
                        anchors { fill: parent; margins: Style.px(6) }
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: cat.open ? Storage.files(cat.modelData.id, 300) : []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: RowLayout {
                            required property var modelData
                            width: list.width - Style.px(10)
                            height: Style.px(24)
                            spacing: Style.px(10)
                            Txt {
                                Layout.fillWidth: true
                                text: modelData.shown
                                elide: Text.ElideMiddle
                                font.family: "Menlo"
                                font.pixelSize: Style.fontSmall
                            }
                            Txt { text: st.size(modelData.bytes); muted: true; font.pixelSize: Style.fontSmall }
                        }
                        footer: Txt {
                            visible: cat.modelData.count > 300
                            height: visible ? Style.px(24) : 0
                            text: qsTr("…and %n more", "", cat.modelData.count - 300)
                            muted: true
                            font.pixelSize: Style.fontSmall
                        }
                    }
                }
            }
        }

        SettingsNote { text: Storage.blocker; kind: "warn" }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Style.px(4)
            spacing: Style.px(12)
            Txt {
                Layout.fillWidth: true
                text: st.chosen.some(c => !c.lossless)
                      ? qsTr("Removed files can't be brought back. Game files are downloaded again when needed.")
                      : ""
                muted: true
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSmall
            }
            PrimaryButton {
                iconName: "trash"
                text: Storage.cleaning ? qsTr("Freeing up…")
                                       : st.chosenBytes > 0 ? qsTr("Free up %1").arg(st.size(st.chosenBytes)) : qsTr("Nothing to free up")
                progress: Storage.cleaning ? -1 : undefined
                enabled: st.chosenBytes > 0 && !Storage.scanning && !Storage.cleaning && Storage.blocker.length === 0
                onClicked: st.confirmClean()
            }
        }
    }

    function confirmClean() {
        const lines = st.chosen.map(c => "• " + st.info(c.id).title + " — " + st.size(c.bytes)
                                         + (c.lossless ? "" : " (" + qsTr("%n file(s)", "", c.count) + ")"))
        const lossy = st.chosen.filter(c => !c.lossless)
        let text = lines.join("\n") + "\n\n"
        if (lossy.length)
            text += qsTr("Removed files can't be brought back.")
        if (st.chosen.some(c => c.lossless))
            text += (lossy.length ? " " : "") + qsTr("Identical mods are linked, not deleted: nothing is lost.")
        ask.ask({ title: qsTr("Free up %1?").arg(st.size(st.chosenBytes)), text: text,
                  buttons: [lossy.length ? qsTr("Remove") : qsTr("Link files"), qsTr("Cancel")],
                  tone: lossy.length ? "danger" : "info" },
                (i) => { if (i === 0) Storage.clean(st.chosen.map(c => c.id)) })
    }

    ConfirmDialog { id: ask }
}
