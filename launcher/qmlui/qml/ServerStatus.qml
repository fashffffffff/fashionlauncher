import QtQuick
import QtQuick.Layouts

// "● 12 / 100 players · 48 ms" — a server's live status in one short line.
RowLayout {
    id: st
    property string status: "unknown"
    property int players: -1
    property int maxPlayers: -1
    property int latency: -1
    property string error
    /// Shown after the status when online, e.g. the server's version
    property string extra
    spacing: Style.px(6)

    readonly property color dot: status === "online" ? (latency > 250 ? "#E0B040" : Style.success)
                               : status === "offline" ? Style.danger : Style.textMuted

    Rectangle {
        Layout.preferredWidth: Style.px(8); Layout.preferredHeight: Style.px(8)
        radius: width / 2
        color: st.dot
        SequentialAnimation on opacity {
            running: st.status === "pinging" && Style.motion
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 500 }
            NumberAnimation { to: 1; duration: 500 }
            onStopped: parent.opacity = 1
        }
    }
    Txt {
        Layout.fillWidth: true
        muted: true
        font.pixelSize: Style.fontSmall
        text: {
            if (st.status === "pinging") return qsTr("Checking…")
            if (st.status === "offline") return qsTr("Offline")
            if (st.status !== "online") return qsTr("Not checked")
            const parts = []
            if (st.players >= 0) parts.push(st.maxPlayers > 0 ? qsTr("%1 / %2 players").arg(st.players).arg(st.maxPlayers) : qsTr("%n player(s)", "", st.players))
            if (st.latency > 0) parts.push(qsTr("%1 ms").arg(st.latency))
            if (st.extra.length) parts.push(st.extra)
            return parts.length ? parts.join("  ·  ") : qsTr("Online")
        }
    }
}
