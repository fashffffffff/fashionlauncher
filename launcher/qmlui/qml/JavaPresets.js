.pragma library

// Ready-made Java argument sets. "balanced" is the default for new players.
// IgnoreUnrecognizedVMOptions keeps older Java (8/17) starting when a flag is newer than it.
var presets = [
    { id: "balanced", args: "-XX:+UseG1GC -XX:+ParallelRefProcEnabled -XX:MaxGCPauseMillis=200 -XX:+UnlockExperimentalVMOptions -XX:+DisableExplicitGC -XX:G1NewSizePercent=30 -XX:G1MaxNewSizePercent=40 -XX:G1HeapRegionSize=8M -XX:G1ReservePercent=20 -XX:G1HeapWastePercent=5 -XX:G1MixedGCCountTarget=4 -XX:InitiatingHeapOccupancyPercent=15 -XX:G1MixedGCLiveThresholdPercent=90 -XX:G1RSetUpdatingPauseTimePercent=5 -XX:SurvivorRatio=32 -XX:+PerfDisableSharedMem -XX:MaxTenuringThreshold=1" },
    { id: "smooth", args: "-XX:+IgnoreUnrecognizedVMOptions -XX:+UseZGC -XX:+ZGenerational -XX:+DisableExplicitGC -XX:+PerfDisableSharedMem" },
    { id: "lowend", args: "-XX:+UseG1GC -XX:MaxGCPauseMillis=50 -XX:G1HeapRegionSize=4M -XX:+DisableExplicitGC -XX:+UseStringDeduplication -XX:+PerfDisableSharedMem" },
    { id: "none", args: "" }
]

function idFor(args) {
    const a = (args || "").trim()
    for (const p of presets) if (p.args === a) return p.id
    return "custom"
}
function argsFor(id) {
    const p = presets.find(p => p.id === id)
    return p ? p.args : ""
}
