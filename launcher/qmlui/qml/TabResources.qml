import QtQuick
import QtQuick.Layouts

// Installed mods / resource packs / shaders of one instance. (Owned by the resources work.)
Item {
    property string instanceId
    property var instance
    property string kind      // "mods" | "resourcepacks" | "shaderpacks"
    StubNotice {
        anchors.fill: parent
        title: qsTr("Installed content")
        body: qsTr("The new manager for mods, resource packs and shaders is on its way.")
        onAction: Launcher.editInstance(parent.instanceId, parent.kind)
    }
}
