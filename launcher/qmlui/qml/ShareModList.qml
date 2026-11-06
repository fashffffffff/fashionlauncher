import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Searchable checklist of a pack's mods and packs. Unticking adds the key to `exclude`.
ColumnLayout {
    id: list
    /// manifest already filtered for server-only/everything (not for exclusions)
    property var manifest: ({})
    property var exclude: []
    property bool loading: false
    signal edited(var exclude)
    readonly property var rows: {
        const out = []
        const groups = [["mods", qsTr("Mods")], ["resourcepacks", qsTr("Resource packs")], ["shaderpacks", qsTr("Shader packs")]]
        const q = search.text.trim().toLowerCase()
        for (const [k, title] of groups) {
            const items = (manifest[k] || []).filter(m => !q || (m.name || "").toLowerCase().includes(q) || (m.file || "").toLowerCase().includes(q))
            if (!items.length) continue
            if (groups.filter(g => (manifest[g[0]] || []).length).length > 1) out.push({ header: title })
            for (const m of items) out.push(m)
        }
        return out
    }
    readonly property int total: ["mods", "resourcepacks", "shaderpacks"].reduce((n, k) => n + (manifest[k] || []).length, 0)
    spacing: Style.px(10)

    function toggle(key) {
        const e = exclude.slice()
        const i = e.indexOf(key)
        if (i >= 0) e.splice(i, 1); else e.push(key)
        edited(e)
    }
    function sideLabel(s) {
        return s === "client" ? qsTr("Client only") : s === "server" ? qsTr("Server only") : s === "both" ? qsTr("Client and server") : qsTr("Not sure")
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(10)
        Field {
            id: search
            Layout.fillWidth: true
            iconName: "search"
            placeholderText: qsTr("Find a mod")
            visible: list.total > 6
        }
        QuietButton {
            visible: list.exclude.length > 0
            plain: true
            text: qsTr("Tick all")
            onClicked: list.edited([])
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Style.px(120)
        radius: Style.radius
        color: Style.alpha(Style.surface, 0.5)
        border.width: 1
        border.color: Style.line
        clip: true

        Txt {
            anchors.centerIn: parent
            visible: view.count === 0
            muted: true
            text: list.loading ? qsTr("Checking your mods…")
                : search.text ? qsTr("Nothing matches “%1”").arg(search.text)
                : qsTr("No mods here — only the Minecraft version and loader will be shared.")
            width: parent.width - Style.px(40)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        ListView {
            id: view
            anchors { fill: parent; margins: Style.px(4) }
            model: list.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: cell
                required property var modelData
                readonly property var entry: modelData
                width: ListView.view.width
                implicitHeight: entry.header ? header.implicitHeight : modRow.implicitHeight
                Txt {
                    id: header
                    visible: !!cell.entry.header
                    text: cell.entry.header || ""
                    font.weight: Font.DemiBold
                    font.pixelSize: Style.fontSmall
                    muted: true
                    topPadding: Style.px(10)
                    bottomPadding: Style.px(4)
                    leftPadding: Style.px(10)
                }
                ShareModRow {
                    id: modRow
                    visible: !cell.entry.header
                    width: parent.width
                    entry: cell.entry
                    on: list.exclude.indexOf(cell.entry.key) < 0
                    sideLabel: list.sideLabel(cell.entry.side)
                    onClicked: list.toggle(cell.entry.key)
                }
            }
        }
    }
}
