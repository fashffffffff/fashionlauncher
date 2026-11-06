import QtQuick
import QtQuick.Layouts

// Snapshots of the instance (last 5 launches) and rollback. (Owned by the store/snapshots work.)
Item {
    property string instanceId
    property var instance
    Txt { anchors.centerIn: parent; text: qsTr("Version history will appear here."); muted: true }
}
