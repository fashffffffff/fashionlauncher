.pragma library

function playTime(seconds) {
    if (!seconds || seconds < 60) return qsTr("Not played yet")
    const h = Math.floor(seconds / 3600), m = Math.floor((seconds % 3600) / 60)
    if (h === 0) return qsTr("%1 min played").arg(m)
    return m > 0 ? qsTr("%1 h %2 min played").arg(h).arg(m) : qsTr("%1 h played").arg(h)
}

function lastPlayed(ms) {
    if (!ms || ms <= 0) return ""
    const d = new Date(ms), now = new Date()
    const day = 24 * 3600 * 1000
    const startToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime()
    if (ms >= startToday) return qsTr("Last played today")
    if (ms >= startToday - day) return qsTr("Last played yesterday")
    const days = Math.ceil((startToday - ms) / day)
    if (days < 7) return qsTr("Last played %1 days ago").arg(days)
    return qsTr("Last played %1").arg(d.toLocaleDateString(Qt.locale(), "d MMM yyyy"))
}

function versionLine(inst) {
    if (!inst) return ""
    const v = inst.gameVersion ? "Minecraft " + inst.gameVersion : ""
    return inst.loader ? v + "  /  " + inst.loader : v
}

// "2 h 15 min" / "45 min" / "3 h" (no "played" suffix), for statistics.
function duration(seconds) {
    const h = Math.floor((seconds || 0) / 3600), m = Math.floor(((seconds || 0) % 3600) / 60)
    if (h === 0) return qsTr("%1 min").arg(m)
    return m > 0 ? qsTr("%1 h %2 min").arg(h).arg(m) : qsTr("%1 h").arg(h)
}
