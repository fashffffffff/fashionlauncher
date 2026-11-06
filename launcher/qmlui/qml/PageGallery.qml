import QtQuick
import QtQuick.Layouts

// Community themes and backgrounds. (Owned by the gallery/sounds work.)
Item {
    property var params: ({})
    StubNotice {
        anchors { fill: parent; margins: Style.px(32) }
        title: qsTr("Theme gallery")
        body: qsTr("Themes and backgrounds made by other players.")
        actionText: qsTr("Back")
        onAction: Router.back()
    }
}
