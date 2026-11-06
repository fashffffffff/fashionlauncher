import QtQuick
import QtQuick.Layouts
import "ResFmt.js" as RF

// Installed mods / resource packs / shaders of one instance, and finding new ones.
Item {
    id: tab
    property string instanceId
    property var instance
    property string kind      // "mods" | "resourcepacks" | "shaderpacks"
    property string mode: "installed"   // "installed" | "find"
    onKindChanged: mode = "installed"

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(14)

        ResSegmented {
            options: [{ value: "installed", label: qsTr("Installed") }, { value: "find", label: RF.findLabel(tab.kind) }]
            value: tab.mode
            onPicked: (v) => tab.mode = v
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: tab.kind.length > 0
            sourceComponent: tab.mode === "find" ? findView : installedView
        }
    }

    Component {
        id: installedView
        ResInstalledView {
            instanceId: tab.instanceId
            kind: tab.kind
            onFindRequested: tab.mode = "find"
        }
    }
    Component {
        id: findView
        ResFindView {
            instanceId: tab.instanceId
            kind: tab.kind
        }
    }
}
