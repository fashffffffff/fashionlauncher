import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Labeled drop-down: shows the current choice, opens a scrollable list.
// options = [{ value, label, icon? }] (icon is an image URL)
Button {
    id: control
    property var options: []
    property string value
    property string label            // shown before the choice, e.g. "Sort"
    property string placeholder: qsTr("Choose")
    signal picked(string value)

    readonly property var current: {
        for (let i = 0; i < options.length; ++i) if (options[i].value === value) return options[i]
        return null
    }

    implicitHeight: Style.px(40)
    implicitWidth: Math.min(Style.px(320), row.implicitWidth + Style.px(26))
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: (label ? label + ": " : "") + (current ? current.label : placeholder)
    onClicked: popup.opened ? popup.close() : popup.open()

    background: Rectangle {
        radius: Style.radius
        color: control.down || popup.opened ? Style.alpha(Style.text, 0.14) : control.hovered ? Style.alpha(Style.text, 0.10) : Style.alpha(Style.surface, 0.72)
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Style.accent : Style.panelBorder
    }
    contentItem: Item {
        RowLayout {
            id: row
            anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(10) }
            spacing: Style.px(8)
            Text {
                visible: control.label.length > 0
                text: control.label
                color: Style.textMuted
                font.family: Style.ui
                font.pixelSize: Style.fontBody
            }
            Image {
                visible: !!(control.current && control.current.icon)
                source: control.current && control.current.icon ? control.current.icon : ""
                Layout.preferredWidth: Style.px(20); Layout.preferredHeight: Style.px(20)
                sourceSize: Qt.size(40, 40)
                smooth: false
            }
            Text {
                Layout.fillWidth: true
                text: control.current ? control.current.label : control.placeholder
                color: Style.text
                font.family: Style.ui
                font.pixelSize: Style.fontBody
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Icon { name: "chevron-right"; rotation: 90; size: Style.px(14); color: Style.textMuted }
        }
    }

    Popup {
        id: popup
        y: control.height + Style.px(4)
        width: Math.max(control.width, Style.px(220))
        height: Math.min(list.contentHeight + topPadding + bottomPadding, Style.px(360))
        padding: Style.px(6)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.dur(110) } }
        ListView {
            id: list
            anchors.fill: parent
            clip: true
            model: control.options
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: ItemDelegate {
                id: opt
                width: ListView.view.width
                implicitHeight: Style.px(36)
                leftPadding: Style.px(10)
                highlighted: modelData.value === control.value
                onClicked: { popup.close(); control.picked(modelData.value) }
                contentItem: RowLayout {
                    spacing: Style.px(10)
                    Image {
                        visible: !!modelData.icon
                        source: modelData.icon || ""
                        Layout.preferredWidth: Style.px(22); Layout.preferredHeight: Style.px(22)
                        sourceSize: Qt.size(44, 44)
                        smooth: false
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: Style.text
                        font.family: Style.ui
                        font.pixelSize: Style.fontBody
                        font.weight: opt.highlighted ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Icon { visible: opt.highlighted; name: "check"; size: Style.px(16); color: Style.accent }
                }
                background: Rectangle {
                    radius: Style.radius - 4
                    color: opt.hovered || opt.visualFocus ? Style.alpha(Style.text, 0.08) : "transparent"
                }
            }
        }
    }
}
