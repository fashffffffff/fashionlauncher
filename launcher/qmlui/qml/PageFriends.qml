import QtQuick
import QtQuick.Layouts

// Friends and playing together without a server. (Owned by the multiplayer work.)
Item {
    property var params: ({})
    StubNotice {
        anchors { fill: parent; margins: Style.px(32) }
        title: qsTr("Friends")
        body: qsTr("Add friends and play together without setting anything up.")
        actionText: qsTr("Back to Home")
        onAction: Router.go("home")
    }
}
