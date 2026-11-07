import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Fmt.js" as Fmt

// "Statistics" tab of the Instances page: a year of play days, totals, streaks, top instances, versions and loaders.
// Data comes from `Stats` (session log + one-time backfill from old game logs). Flat theme colours only.
Flickable {
    id: root
    clip: true
    contentWidth: width
    contentHeight: col.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    // Recomputed whenever the log grows, and each time the tab is shown (play time of a running game moves on).
    readonly property var s: (Stats.revision >= 0 && visible) ? Stats.summary(Qt.locale().firstDayOfWeek) : ({})
    readonly property bool empty: !(s.allTime > 0)
    Component.onCompleted: Stats.ensureBackfill()
    onVisibleChanged: if (visible) Stats.ensureBackfill()

    // Heatmap tints: an opaque mix of the panel surface and the accent, so cells stay flat in any theme.
    function tint(level) {
        if (level <= 0) return Style.alpha(Style.text, Style.dark ? 0.07 : 0.08)
        return Style.mix(Style.surface, Style.accent, [0, 0.3, 0.52, 0.76, 1][level])
    }
    function dateOf(iso) { const p = iso.split("-"); return new Date(+p[0], +p[1] - 1, +p[2]) }

    component Card: Rectangle {
        default property alias content: inner.data
        property string title: ""
        property alias trailing: trail.data
        radius: Style.radius
        color: Style.panel(0)
        border.width: 1
        border.color: Style.panelBorder
        implicitHeight: inner.implicitHeight + head.implicitHeight + Style.px(20 + 18 + 14)
        RowLayout {
            id: head
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(20) }
            Txt { text: parent.parent.title; font.pixelSize: Style.fontLead; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Row { id: trail; spacing: Style.px(6) }
        }
        ColumnLayout {
            id: inner
            anchors { left: parent.left; right: parent.right; top: head.bottom; margins: Style.px(20); topMargin: Style.px(14) }
            spacing: Style.px(10)
        }
    }

    // A label, a value, and a flat bar proportional to `ratio`.
    component BarRow: ColumnLayout {
        property string label
        property string value
        property real ratio: 0
        property url icon: ""
        Layout.fillWidth: true
        spacing: Style.px(6)
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(10)
            Image {
                visible: parent.parent.icon.toString().length > 0
                source: parent.parent.icon
                Layout.preferredWidth: Style.px(24)
                Layout.preferredHeight: Style.px(24)
                sourceSize: Qt.size(48, 48)
                smooth: false
            }
            Txt { text: parent.parent.label; Layout.fillWidth: true; font.weight: Font.Medium }
            Txt { text: parent.parent.value; muted: true }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Style.px(8)
            radius: Math.min(Style.radius, height / 2)
            color: Style.alpha(Style.text, 0.08)
            Rectangle {
                width: Math.max(height, parent.width * Math.min(1, parent.parent.ratio))
                height: parent.height
                radius: parent.radius
                color: Style.accent
            }
        }
    }

    ColumnLayout {
        id: col
        width: root.width - Style.px(14)
        spacing: Style.px(16)

        // ---- Totals ----
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > Style.px(1100) ? 6 : 3
            columnSpacing: Style.px(12)
            rowSpacing: Style.px(12)
            Repeater {
                model: [
                    { label: qsTr("This week"), value: Fmt.duration(root.s.week) },
                    { label: qsTr("This month"), value: Fmt.duration(root.s.month) },
                    { label: qsTr("All time"), value: Fmt.duration(root.s.allTime) },
                    { label: qsTr("Average session"), value: root.s.sessions > 0 ? Fmt.duration(root.s.avgSession) : "—" },
                    { label: qsTr("Current streak"), value: qsTr("%n day(s)", "", root.s.currentStreak || 0) },
                    { label: qsTr("Longest streak"), value: qsTr("%n day(s)", "", root.s.longestStreak || 0) },
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: Style.px(88)
                    radius: Style.radius
                    color: Style.panel(0)
                    border.width: 1
                    border.color: Style.panelBorder
                    ColumnLayout {
                        anchors { fill: parent; margins: Style.px(16) }
                        spacing: Style.px(6)
                        Txt { text: modelData.label; muted: true; font.pixelSize: Style.fontSmall; Layout.fillWidth: true }
                        Text {
                            text: modelData.value
                            color: Style.text
                            font.family: Style.display
                            font.pixelSize: Style.px(22)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        // ---- Year heatmap ----
        Card {
            id: year
            Layout.fillWidth: true
            title: qsTr("Last 12 months")
            trailing: [
                Txt { text: qsTr("Less"); muted: true; font.pixelSize: Style.fontSmall; anchors.verticalCenter: parent.verticalCenter },
                Repeater {
                    model: 5
                    delegate: Rectangle {
                        required property int index
                        width: Style.px(12); height: width; radius: Style.px(2)
                        color: root.tint(index)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                },
                Txt { text: qsTr("More"); muted: true; font.pixelSize: Style.fontSmall; anchors.verticalCenter: parent.verticalCenter }
            ]

            Item {
                id: heat
                readonly property var cells: root.s.cells || []
                readonly property int weeks: Math.ceil(cells.length / 7)
                readonly property real labelW: Style.px(34)
                readonly property real gap: Style.px(3)
                readonly property real cell: Math.max(Style.px(6), Math.min(Style.px(16), (width - labelW) / Math.max(1, weeks) - gap))
                readonly property real pitch: cell + gap
                readonly property real topPad: Style.px(20)   // month labels
                Layout.fillWidth: true
                implicitHeight: topPad + 7 * pitch

                // Month labels at the first column whose week starts a new month.
                Repeater {
                    model: {
                        const out = []
                        let last = -1
                        for (let w = 0; w < heat.weeks; ++w) {
                            const c = heat.cells[w * 7]
                            if (!c) continue
                            const m = root.dateOf(c.date).getMonth()
                            if (m !== last) { if (w < heat.weeks - 2 && (out.length === 0 || w - out[out.length - 1].w > 2)) out.push({ w: w, m: m }); last = m }
                        }
                        return out
                    }
                    delegate: Txt {
                        required property var modelData
                        x: heat.labelW + modelData.w * heat.pitch
                        y: 0
                        text: Qt.locale().standaloneMonthName(modelData.m, Locale.ShortFormat)
                        muted: true
                        font.pixelSize: Style.fontSmall
                    }
                }
                // Weekday labels on every other row.
                Repeater {
                    model: [0, 2, 4]
                    delegate: Txt {
                        required property int modelData
                        readonly property var c: heat.cells[modelData]
                        y: heat.topPad + modelData * heat.pitch + (heat.cell - height) / 2
                        text: c ? Qt.locale().dayName(root.dateOf(c.date).getDay(), Locale.ShortFormat) : ""
                        muted: true
                        font.pixelSize: Style.fontSmall
                    }
                }
                Repeater {
                    model: heat.cells
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        x: heat.labelW + Math.floor(index / 7) * heat.pitch
                        y: heat.topPad + (index % 7) * heat.pitch
                        width: heat.cell
                        height: heat.cell
                        radius: Math.min(Style.px(3), Style.radius / 3)
                        color: root.tint(modelData.level)
                        border.width: hover.cell === index ? 1 : 0
                        border.color: Style.text
                    }
                }
                MouseArea {
                    id: hover
                    property int cell: -1
                    x: heat.labelW
                    y: heat.topPad
                    width: heat.weeks * heat.pitch
                    height: 7 * heat.pitch
                    hoverEnabled: true
                    onPositionChanged: (m) => {
                        const i = Math.floor(m.x / heat.pitch) * 7 + Math.floor(m.y / heat.pitch)
                        cell = (i >= 0 && i < heat.cells.length) ? i : -1
                    }
                    onExited: cell = -1
                }
                // Tooltip "7 November 2026 — 2 h 15 min"
                Rectangle {
                    id: tip
                    readonly property var c: hover.cell >= 0 ? heat.cells[hover.cell] : null
                    visible: c !== null
                    z: 2
                    width: tipText.implicitWidth + Style.px(20)
                    height: tipText.implicitHeight + Style.px(12)
                    radius: Style.radius / 2
                    color: Style.surfaceHi
                    border.width: 1
                    border.color: Style.line
                    readonly property real cx: heat.labelW + Math.floor(hover.cell / 7) * heat.pitch + heat.cell / 2
                    x: Math.max(0, Math.min(heat.width - width, cx - width / 2))
                    y: heat.topPad + (hover.cell % 7) * heat.pitch - height - Style.px(6)
                    Txt {
                        id: tipText
                        anchors.centerIn: parent
                        text: tip.c ? root.dateOf(tip.c.date).toLocaleDateString(Qt.locale(), "d MMMM yyyy") + " — "
                                      + (tip.c.secs >= 60 ? Fmt.duration(tip.c.secs) : qsTr("no play")) : ""
                    }
                }
            }
            Txt {
                visible: Stats.backfilling || root.s.backfilled > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                font.pixelSize: Style.fontSmall
                text: Stats.backfilling ? qsTr("Reading old game logs…")
                                        : qsTr("Includes %n session(s) recovered from old game logs.", "", root.s.backfilled || 0)
            }
        }

        // ---- Top instances | versions and loaders ----
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > Style.px(860) ? 2 : 1
            columnSpacing: Style.px(16)
            rowSpacing: Style.px(16)

            Card {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                title: qsTr("Most played instances")
                Repeater {
                    model: root.s.topInstances || []
                    delegate: BarRow {
                        required property var modelData
                        icon: modelData.icon
                        label: modelData.name
                        value: Fmt.duration(modelData.secs)
                        ratio: modelData.secs / Math.max(1, root.s.topInstances[0].secs)
                        TapHandler { onTapped: Router.openInstance(modelData.id) }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
                Txt { visible: root.empty; text: qsTr("Nothing played yet."); muted: true }
            }

            Card {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                title: qsTr("Versions and loaders")
                Repeater {
                    model: root.s.versions || []
                    delegate: BarRow {
                        required property var modelData
                        label: "Minecraft " + modelData.label
                        value: Fmt.duration(modelData.secs)
                        ratio: modelData.secs / Math.max(1, root.s.versions[0].secs)
                    }
                }
                Rectangle {
                    visible: (root.s.versions || []).length > 0 && (root.s.loaders || []).length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: Style.px(4)
                    Layout.bottomMargin: Style.px(4)
                    implicitHeight: 1
                    color: Style.panelBorder
                }
                Repeater {
                    model: root.s.loaders || []
                    delegate: BarRow {
                        required property var modelData
                        label: modelData.label || qsTr("Vanilla")
                        value: Fmt.duration(modelData.secs)
                        ratio: modelData.secs / Math.max(1, root.s.loaders[0].secs)
                    }
                }
                Txt { visible: root.empty; text: qsTr("Nothing played yet."); muted: true }
            }
        }

        Txt {
            Layout.fillWidth: true
            Layout.bottomMargin: Style.px(8)
            wrapMode: Text.WordWrap
            muted: true
            font.pixelSize: Style.fontSmall
            text: qsTr("%n session(s) recorded. Sessions are saved when the game closes.", "", root.s.sessions || 0)
        }
    }
}
