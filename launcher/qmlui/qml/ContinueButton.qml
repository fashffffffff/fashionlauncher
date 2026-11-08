import QtQuick
import QtQuick.Controls

// "Continue": start the instance straight in the world or on the server played last (Quick Play).
// A neutral block next to Play; hidden when the game version can't do it or nothing was played yet.
PrimaryButton {
    id: btn
    property string instanceId
    property var target: ({})
    readonly property bool busy: instanceId.length > 0 && Launcher.launches[instanceId] !== undefined

    function refresh() { target = instanceId ? QuickPlay.last(instanceId) : ({}) }
    onInstanceIdChanged: refresh()
    Component.onCompleted: refresh()
    // a finished game may have left a newer world or server behind
    onBusyChanged: if (!busy) refresh()

    visible: !!target.kind && !busy
    text: target.kind === "server" ? qsTr("Continue on %1").arg(target.name) : qsTr("Continue: %1").arg(target.name || "")
    iconName: target.kind === "server" ? "globe" : "world"
    fill: Style.surfaceHi
    sound: "launch"
    ToolTip.visible: hovered
    ToolTip.delay: 500
    ToolTip.text: target.kind === "server" ? qsTr("Start the game and join %1 right away").arg(target.value)
                                           : qsTr("Start the game straight in this world")
    onClicked: QuickPlay.play(instanceId, target.kind, target.value)
}
