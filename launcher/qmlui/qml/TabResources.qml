import QtQuick
import QtQuick.Layouts

// Installed mods / resource packs / shaders of one instance. Finding new ones opens the catalog
// as its own full-size page (PageBrowse) — squeezed under the instance header it showed too little.
Item {
    id: tab
    property string instanceId
    property var instance
    property string kind      // "mods" | "resourcepacks" | "shaderpacks"

    ResInstalledView {
        anchors.fill: parent
        visible: tab.kind.length > 0
        instanceId: tab.instanceId
        kind: tab.kind
        onFindRequested: Router.open("browse", { instanceId: tab.instanceId, kind: tab.kind, fromInstance: true })
    }
}
