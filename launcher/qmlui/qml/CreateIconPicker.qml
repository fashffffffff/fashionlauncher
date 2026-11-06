import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Pick the new instance's icon from the icon library, or add an image file.
Popup {
    id: picker
    property string currentKey: "default"
    property string autoLabel: ""          // e.g. "Modpack logo"; empty hides the option
    property string autoSource: ""
    signal chosen(string key)              // "" means the automatic icon

    width: Style.px(380)
    height: Style.px(360)
    padding: Style.px(14)
    modal: true
    focus: true
    onOpened: icons.model = Creator.icons()
    background: Rectangle {
        radius: Style.radius
        color: Style.surface
        border.width: 1
        border.color: Style.line
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose an image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.svg *.ico *.gif *.webp)")]
        onAccepted: {
            const key = Creator.addIconFile(selectedFile.toString())
            if (key.length) { picker.chosen(key); picker.close() }
            else Router.toast(qsTr("That image couldn't be added."), true)
        }
    }

    contentItem: ColumnLayout {
        spacing: Style.px(10)
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Icon")
                color: Style.text
                font.family: Style.ui
                font.pixelSize: Style.fontLead
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            QuietButton { text: qsTr("Add image…"); iconName: "image"; onClicked: fileDialog.open() }
        }
        QuietButton {
            visible: picker.autoLabel.length > 0
            Layout.fillWidth: true
            text: picker.autoLabel
            iconName: picker.currentKey === "" ? "check" : ""
            onClicked: { picker.chosen(""); picker.close() }
        }
        GridView {
            id: icons
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(width / 6)
            cellHeight: cellWidth
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: cell
                width: icons.cellWidth
                height: icons.cellHeight
                hoverEnabled: true
                Accessible.name: modelData.name
                ToolTip.visible: hovered
                ToolTip.text: modelData.name
                ToolTip.delay: 600
                onClicked: { picker.chosen(modelData.key); picker.close() }
                background: Rectangle {
                    anchors.fill: parent
                    anchors.margins: Style.px(2)
                    radius: Style.radius - 4
                    color: modelData.key === picker.currentKey ? Style.alpha(Style.accent, 0.3)
                         : cell.hovered ? Style.alpha(Style.text, 0.08) : "transparent"
                    border.width: cell.visualFocus ? 2 : 0
                    border.color: Style.accent
                }
                contentItem: Item {
                    Image {
                        anchors.centerIn: parent
                        width: Math.round(cell.width * 0.62)
                        height: width
                        source: "image://instanceicon/" + modelData.key
                        sourceSize: Qt.size(64, 64)
                        smooth: false
                    }
                }
            }
        }
    }
}
