import QtQuick
import QtQuick.Layouts

Item {
    property string instanceId
    property var instance
    StubNotice {
        anchors.fill: parent
        title: qsTr("Worlds")
        body: qsTr("Back up, copy and delete the worlds of this instance.")
        onAction: Launcher.editInstance(parent.instanceId, "worlds")
    }
}
