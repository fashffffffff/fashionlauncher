import QtQuick
import QtQuick.Layouts

// Create an instance: version + loader, modpack, or import. (Owned by the create work.)
Item {
    property var params: ({})
    StubNotice {
        anchors { fill: parent; margins: Style.px(32) }
        title: qsTr("Create instance")
        body: qsTr("Pick a Minecraft version and mod loader, or install a modpack.")
        onAction: Launcher.newInstance()
    }
}
