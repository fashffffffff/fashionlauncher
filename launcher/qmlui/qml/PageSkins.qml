import QtQuick
import QtQuick.Layouts

// Skins with a 3D preview. (Owned by the skins/servers work.)
Item {
    property var params: ({})
    StubNotice {
        anchors { fill: parent; margins: Style.px(32) }
        title: qsTr("Skins")
        body: qsTr("Preview, collect and change your skin.")
        onAction: Launcher.openSettings("accounts")
    }
}
