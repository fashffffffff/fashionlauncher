import QtQuick
import QtQuick.Layouts

// Per-instance settings: Java, memory, window, overrides of global settings. (Owned by the settings work.)
Item {
    property string instanceId
    property var instance
    StubNotice {
        anchors.fill: parent
        title: qsTr("Instance settings")
        body: qsTr("Java, memory and game window settings for this instance.")
        onAction: Launcher.editInstance(parent.instanceId, "settings")
    }
}
