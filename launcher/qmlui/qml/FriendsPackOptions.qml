import QtQuick
import QtQuick.Layouts

// "Server-side mods only" or "Everything", plus switching off single mods.
// manifest.mods: [{ id, name, side: "client" | "server" | "both" }]
ColumnLayout {
    id: opts
    property var manifest: null
    property string mode: "server"          // "server" | "all"
    property var exclude: []
    readonly property bool includeClient: mode === "all"
    readonly property var mods: manifest && manifest.mods ? manifest.mods : []
    readonly property var shown: mods.filter(m => includeClient || m.side !== "client")
    spacing: Style.px(10)

    SettingsSegmented {
        options: [{ value: "server", label: qsTr("Server-side mods only") }, { value: "all", label: qsTr("Everything") }]
        value: opts.mode
        onPicked: v => opts.mode = v
    }
    Txt {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        muted: true
        font.pixelSize: Style.fontSmall
        text: opts.mode === "server"
            ? qsTr("Only what's needed to join. Your own client mods (shaders, minimaps, performance) stay as they are.")
            : qsTr("The whole pack, including client-side mods like minimaps and shaders.")
    }
    Txt {
        visible: opts.shown.length > 0
        text: qsTr("Mods (tap to leave one out)")
        font.weight: Font.DemiBold
    }
    Flow {
        visible: opts.shown.length > 0
        Layout.fillWidth: true
        spacing: Style.px(6)
        Repeater {
            model: opts.shown
            delegate: CreateChip {
                text: modelData.name || modelData.id
                checked: opts.exclude.indexOf(modelData.id) < 0
                onToggled: {
                    const ex = opts.exclude.filter(x => x !== modelData.id)
                    if (!checked) ex.push(modelData.id)
                    opts.exclude = ex
                }
            }
        }
    }
}
