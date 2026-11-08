import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ResFmt.js" as RF

// One search result: icon, title, author, downloads, summary and a Select toggle for the batch install.
// Used as a delegate of SearchModel (reads the model roles).
AbstractButton {
    id: card
    property bool current: false
    signal pickToggled()

    implicitHeight: Style.px(96)
    hoverEnabled: true
    Accessible.name: model.title

    background: Rectangle {
        radius: Style.radius
        color: card.current ? Style.alpha(Style.accent, 0.12) : card.hovered ? Style.panel(1) : Style.alpha(Style.text, 0.03)
        border.width: card.visualFocus || card.current ? 2 : 1
        border.color: card.visualFocus || card.current ? Style.accent : Style.panelBorder
    }

    contentItem: RowLayout {
        spacing: Style.px(14)
        anchors { fill: parent; leftMargin: Style.px(14); rightMargin: Style.px(14) }

        Rectangle {
            Layout.preferredWidth: Style.px(60); Layout.preferredHeight: Style.px(60)
            radius: Style.radius - 2
            color: Style.alpha(Style.text, 0.06)
            clip: true
            Image {
                id: logo
                anchors.fill: parent
                source: model.iconUrl
                sourceSize: Qt.size(120, 120)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: status === Image.Ready
            }
            Icon { anchors.centerIn: parent; visible: !logo.visible; name: "cube"; size: Style.px(24); color: Style.textMuted }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.px(3)
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(8)
                Txt {
                    Layout.maximumWidth: card.width * 0.5
                    text: model.title
                    font.pixelSize: Style.fontLead
                    font.weight: Font.DemiBold
                }
                Txt {
                    Layout.fillWidth: true
                    visible: !!model.author
                    text: qsTr("by %1").arg(model.author)
                    muted: true
                }
            }
            Txt {
                Layout.fillWidth: true
                text: model.summary
                muted: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }
            Txt {
                visible: model.downloads >= 0
                text: qsTr("%1 downloads").arg(RF.count(model.downloads))
                muted: true
                font.pixelSize: Style.fontSmall
            }
        }

        // Pick for the batch install / queued / progress / installed
        Item {
            Layout.preferredWidth: Style.px(140)
            Layout.preferredHeight: Style.px(40)
            QuietButton {
                anchors.fill: parent
                visible: model.state === ""
                text: model.picked ? qsTr("Selected") : qsTr("Select")
                iconName: model.picked ? "check" : "plus"
                sound: ""
                background: Rectangle {
                    radius: Style.radius
                    color: model.picked ? Style.alpha(Style.accent, parent.hovered ? 0.30 : 0.22)
                         : parent.hovered ? Style.alpha(Style.text, 0.10) : Style.alpha(Style.surface, 0.72)
                    border.width: parent.visualFocus || model.picked ? 2 : 1
                    border.color: parent.visualFocus || model.picked ? Style.accent : Style.alpha(Style.text, 0.08)
                }
                onClicked: { Sounds.play("click"); card.pickToggled() }
            }
            Txt {
                anchors.centerIn: parent
                visible: model.state === "queued"
                text: qsTr("In queue")
                muted: true
                font.weight: Font.DemiBold
            }
            PrimaryButton {
                anchors.fill: parent
                visible: model.state === "installing"
                text: qsTr("Installing…")
                enabled: false
                opacity: 1
                progress: model.progress
            }
            Row {
                anchors.centerIn: parent
                visible: model.state === "installed"
                spacing: Style.px(6)
                Icon { name: "check"; size: Style.px(18); color: Style.success; anchors.verticalCenter: parent.verticalCenter }
                Txt { text: qsTr("Installed"); font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
}
