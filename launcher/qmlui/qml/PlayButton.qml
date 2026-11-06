import QtQuick

// Play / launching progress / Stop for one instance.
PrimaryButton {
    id: btn
    property string instanceId
    readonly property var state: instanceId ? Launcher.launches[instanceId] : undefined
    readonly property string phase: state ? state.phase : ""
    text: phase === "running" ? qsTr("Stop") : phase === "launching" ? state.status : qsTr("Play")
    iconName: phase === "running" ? "stop" : phase === "launching" ? "" : "play"
    progress: phase === "launching" ? state.progress : undefined
    fill: phase === "running" ? Style.surfaceHi : Style.accent
    ink: phase === "running" ? Style.text : Style.onAccent
    sound: phase === "" ? "launch" : "click"
    onClicked: {
        if (!instanceId) return
        if (phase === "") Launcher.launch(instanceId)
        else Launcher.kill(instanceId)
    }
}
