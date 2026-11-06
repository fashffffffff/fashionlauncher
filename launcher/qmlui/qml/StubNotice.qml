import QtQuick
import QtQuick.Layouts

// Placeholder for a screen that is being rebuilt; offers the classic Prism screen meanwhile.
ColumnLayout {
    property string title
    property string body
    property string actionText: qsTr("Open classic screen")
    signal action()
    spacing: Style.px(12)
    Text { text: parent.title; color: Style.text; font.family: Style.display; font.pixelSize: Style.fontTitle; font.weight: Font.DemiBold }
    Txt { text: parent.body; muted: true; wrapMode: Text.WordWrap; Layout.maximumWidth: Style.px(560); Layout.fillWidth: true }
    QuietButton { text: parent.actionText; iconName: "link"; onClicked: parent.action() }
    Item { Layout.fillHeight: true }
}
