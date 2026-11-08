import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A button that shows one instance (icon, name, version) and opens a list to pick another.
// With allowAll the list starts with "All instances" (value "*").
AbstractButton {
    id: chooser
    property string value: ""
    property string exceptId: ""
    property bool allowAll: false
    property string placeholder: qsTr("Choose an instance")
    signal chosen(string id)

    readonly property var info: {
        Instances.count
        if (!value || value === "*") return null
        const i = Instances.indexOf(value)
        return i >= 0 ? Instances.get(i) : null
    }
    function versionLine(o) { return o ? [o.gameVersion, o.loader].filter(s => !!s).join(" · ") : "" }

    implicitHeight: Style.px(60)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: info ? info.name : (value === "*" ? qsTr("All instances") : placeholder)
    onClicked: list.open()

    background: Rectangle {
        radius: Style.radius
        color: chooser.hovered || list.opened ? Style.surfaceHi : Style.alpha(Style.surfaceHi, 0.5)
        border.width: chooser.visualFocus || list.opened ? 2 : 1
        border.color: chooser.visualFocus || list.opened ? Style.accent : Style.line
    }
    contentItem: RowLayout {
        spacing: Style.px(10)
        Item {
            Layout.leftMargin: Style.px(10)
            Layout.preferredWidth: Style.px(36); Layout.preferredHeight: Style.px(36)
            Image {
                anchors.fill: parent
                visible: !!chooser.info
                source: chooser.info ? chooser.info.icon : ""
                sourceSize: Qt.size(72, 72)
                fillMode: Image.PreserveAspectFit
                smooth: false
            }
            Icon {
                anchors.centerIn: parent
                visible: !chooser.info
                name: chooser.value === "*" ? "grid" : "cube"
                size: Style.px(22)
                color: Style.textMuted
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.px(2)
            Txt {
                Layout.fillWidth: true
                text: chooser.info ? chooser.info.name : (chooser.value === "*" ? qsTr("All instances") : chooser.placeholder)
                font.weight: Font.DemiBold
                muted: !chooser.info && chooser.value !== "*"
            }
            Txt {
                Layout.fillWidth: true
                visible: text.length > 0
                text: chooser.info ? chooser.versionLine(chooser.info)
                    : chooser.value === "*" ? qsTr("%n instance(s)", "", Math.max(0, Instances.count - 1)) : ""
                muted: true
                font.pixelSize: Style.fontSmall
            }
        }
        Icon {
            Layout.rightMargin: Style.px(12)
            name: "chevron-right"
            rotation: 90
            size: Style.px(14)
            color: Style.textMuted
        }
    }

    Popup {
        id: list
        y: chooser.height + Style.px(4)
        width: Math.max(chooser.width, Style.px(260))
        height: Math.min(view.contentHeight + padding * 2, Style.px(320))
        padding: Style.px(6)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.dur(110) } }

        ListView {
            id: view
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            header: Column {
                width: view.width
                visible: chooser.allowAll
                height: visible ? implicitHeight : 0
                Row_ {
                    iconUrl: ""
                    title: qsTr("All instances")
                    sub: qsTr("%n instance(s)", "", Math.max(0, Instances.count - 1))
                    current: chooser.value === "*"
                    onPicked: { list.close(); chooser.chosen("*") }
                }
                Rectangle { width: parent.width; height: 1; color: Style.line }
            }
            model: Instances
            delegate: Row_ {
                readonly property string rowId: model.instanceId
                visible: rowId !== chooser.exceptId
                height: visible ? implicitHeight : 0
                iconUrl: model.icon
                title: model.name
                sub: [model.gameVersion, model.loader].filter(s => !!s).join(" · ")
                current: rowId === chooser.value
                onPicked: { list.close(); chooser.chosen(rowId) }
            }
        }
    }

    component Row_: AbstractButton {
        id: row
        property string iconUrl
        property string title
        property string sub
        property bool current: false
        signal picked()
        width: ListView.view ? ListView.view.width : parent.width
        implicitHeight: Style.px(46)
        hoverEnabled: true
        onClicked: picked()
        Accessible.name: title
        background: Rectangle {
            radius: Style.radius - 4
            color: row.hovered ? Style.alpha(Style.text, 0.08) : row.current ? Style.alpha(Style.accent, 0.14) : "transparent"
        }
        contentItem: RowLayout {
            spacing: Style.px(10)
            Item {
                Layout.leftMargin: Style.px(6)
                Layout.preferredWidth: Style.px(28); Layout.preferredHeight: Style.px(28)
                Image {
                    anchors.fill: parent
                    visible: row.iconUrl.length > 0
                    source: row.iconUrl
                    sourceSize: Qt.size(56, 56)
                    fillMode: Image.PreserveAspectFit
                    smooth: false
                }
                Icon { anchors.centerIn: parent; visible: !row.iconUrl.length; name: "grid"; size: Style.px(18); color: Style.textMuted }
            }
            Txt { text: row.title; Layout.fillWidth: true }
            Txt { text: row.sub; muted: true; font.pixelSize: Style.fontSmall; Layout.rightMargin: Style.px(8) }
            Icon { visible: row.current; name: "check"; size: Style.px(14); color: Style.accent; Layout.rightMargin: Style.px(8) }
        }
    }
}
