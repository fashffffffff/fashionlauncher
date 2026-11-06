import QtQuick
import QtQuick.Layouts

// Launcher settings. (Owned by the settings work.)
Item {
    property var params: ({})
    StubNotice {
        anchors { fill: parent; margins: Style.px(32) }
        title: qsTr("Settings")
        body: qsTr("Language, Java, memory and launcher behaviour.")
        onAction: Launcher.openSettings("")
    }
}
