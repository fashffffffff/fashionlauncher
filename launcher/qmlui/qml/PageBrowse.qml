import QtQuick
import QtQuick.Layouts

// Discover mods, resource packs, shaders and modpacks on Modrinth and CurseForge. (Owned by the resources work.)
Item {
    property var params: ({})
    StubNotice {
        anchors { fill: parent; margins: Style.px(32) }
        title: qsTr("Browse mods")
        body: qsTr("Search Modrinth and CurseForge and install into any instance.")
        actionText: qsTr("Open instances")
        onAction: Router.go("instances")
    }
}
