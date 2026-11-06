import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pick one value from a list. options: [{ value, label }]
AbstractButton {
    id: dd
    property var options: []
    property var value
    property string placeholder: qsTr("Choose…")
    signal picked(var value)

    readonly property var current: options.find(o => o.value === value)
    implicitHeight: Style.px(40)
    implicitWidth: Style.px(240)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: current ? current.label : placeholder
    onClicked: menu.popup(dd, 0, dd.height + Style.px(4))

    background: Rectangle {
        radius: Style.radius
        color: dd.hovered ? Style.alpha(Style.surfaceHi, 0.95) : Style.alpha(Style.surface, 0.85)
        border.width: dd.visualFocus || menu.visible ? 2 : 1
        border.color: dd.visualFocus || menu.visible ? Style.accent : Style.line
    }
    contentItem: RowLayout {
        spacing: Style.px(8)
        Txt {
            Layout.fillWidth: true
            Layout.leftMargin: Style.px(12)
            text: dd.current ? dd.current.label : dd.placeholder
            muted: !dd.current
        }
        Icon {
            name: "chevron-right"
            rotation: 90
            size: Style.px(16)
            color: Style.textMuted
            Layout.rightMargin: Style.px(12)
        }
    }

    ThemedMenu {
        id: menu
        width: Math.max(dd.width, Style.px(220))
        Instantiator {
            model: dd.options
            delegate: ThemedMenuItem {
                text: modelData.label
                iconName: modelData.value === dd.value ? "check" : ""
                onTriggered: dd.picked(modelData.value)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
}
