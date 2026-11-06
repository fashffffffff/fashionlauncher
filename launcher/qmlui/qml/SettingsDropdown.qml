import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pick one value from a list. options: [{ value, label }]
AbstractButton {
    id: dd
    property var options: []
    property var value
    property string placeholder: qsTr("Choose…")
    /// Long lists (languages) open a searchable list instead of a plain menu
    property bool searchable: options.length > 12
    signal picked(var value)

    readonly property var current: options.find(o => o.value === value)
    implicitHeight: Style.px(40)
    implicitWidth: Style.px(240)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: current ? current.label : placeholder
    onClicked: {
        if (searchable) { search.text = ""; finder.open(); search.forceActiveFocus() }
        else menu.popup(dd, 0, dd.height + Style.px(4))
    }

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

    Popup {
        id: finder
        y: dd.height + Style.px(4)
        width: Math.max(dd.width, Style.px(280))
        height: Style.px(380)
        padding: Style.px(8)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        contentItem: ColumnLayout {
            spacing: Style.px(6)
            Field {
                id: search
                Layout.fillWidth: true
                iconName: "search"
                placeholderText: qsTr("Search")
                Keys.onReturnPressed: if (found.count > 0) { dd.picked(found.model[0].value); finder.close() }
                Keys.onDownPressed: found.forceActiveFocus()
            }
            ListView {
                id: found
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                model: {
                    const q = search.text.trim().toLowerCase()
                    return q ? dd.options.filter(o => String(o.label).toLowerCase().indexOf(q) >= 0
                                                    || String(o.value).toLowerCase().indexOf(q) >= 0)
                             : dd.options
                }
                delegate: AbstractButton {
                    id: opt
                    width: ListView.view.width - Style.px(8)
                    height: Style.px(36)
                    hoverEnabled: true
                    onClicked: { dd.picked(modelData.value); finder.close() }
                    background: Rectangle {
                        radius: Style.radius - 2
                        color: modelData.value === dd.value ? Style.alpha(Style.accent, 0.18)
                             : opt.hovered || opt.activeFocus ? Style.alpha(Style.text, 0.07) : "transparent"
                    }
                    contentItem: RowLayout {
                        Txt { text: modelData.label; Layout.fillWidth: true; Layout.leftMargin: Style.px(10) }
                        Icon { visible: modelData.value === dd.value; name: "check"; color: Style.accent; size: Style.px(16); Layout.rightMargin: Style.px(8) }
                    }
                }
                Txt { anchors.centerIn: parent; visible: found.count === 0; text: qsTr("Nothing found"); muted: true }
            }
        }
    }
}
