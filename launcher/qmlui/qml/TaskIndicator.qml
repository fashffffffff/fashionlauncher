import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Global "something is running" pill, shown on every page while Tasks has work (installs, mod downloads,
// updates). Click opens the list with per-task progress and cancel.
Item {
    id: root
    signal finished(string message, bool error)

    readonly property bool active: Tasks.busy
    readonly property real prog: Tasks.progress
    implicitWidth: pill.width
    implicitHeight: pill.height
    visible: opacity > 0
    opacity: active ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Style.dur(200) } }

    function pct(p) { return p >= 0 ? Math.round(p * 100) + "%" : "" }

    Connections {
        target: Tasks
        function onTaskFinished(title, ok, error) {
            root.finished(ok ? qsTr("Done: %1").arg(title)
                             : qsTr("Failed: %1").arg(title) + (error.length ? " — " + error : ""), !ok)
        }
        function onCountChanged() { if (Tasks.count === 0) list.close() }
    }

    Rectangle {
        id: pill
        width: Math.min(Style.px(380), row.implicitWidth + Style.px(28))
        height: Style.px(44)
        radius: height / 2
        color: Style.surface
        border.width: 1
        border.color: hover.hovered ? Style.accent : Style.line
        clip: true

        // progress fill along the bottom edge
        Rectangle {
            anchors { left: parent.left; bottom: parent.bottom }
            height: Style.px(3)
            color: Style.accent
            width: root.prog >= 0 ? parent.width * root.prog : parent.width * 0.3
            x: 0
            Behavior on width { NumberAnimation { duration: Style.dur(250) } }
            SequentialAnimation on opacity {
                running: root.prog < 0 && root.active
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.3; duration: 700 }
                NumberAnimation { from: 0.3; to: 1; duration: 700 }
            }
        }

        RowLayout {
            id: row
            anchors { fill: parent; leftMargin: Style.px(14); rightMargin: Style.px(14) }
            spacing: Style.px(10)
            Icon {
                name: "refresh"
                size: Style.px(18)
                color: Style.accent
                RotationAnimation on rotation {
                    running: root.active && Style.motion
                    loops: Animation.Infinite
                    from: 0; to: 360; duration: 1200
                }
            }
            Txt {
                Layout.fillWidth: true
                Layout.maximumWidth: Style.px(240)
                text: Tasks.count > 1 ? qsTr("%1 (+%n more)", "", Tasks.count - 1).arg(Tasks.title) : Tasks.title
                font.weight: Font.DemiBold
            }
            Txt {
                text: root.pct(root.prog)
                muted: true
                visible: text.length > 0
                font.features: { "tnum": 1 }
            }
        }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: list.opened ? list.close() : list.open() }
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Running tasks")
    }

    Popup {
        id: list
        y: -height - Style.px(8)
        x: (pill.width - width) / 2
        width: Style.px(420)
        height: Math.min(Style.px(420), col.implicitHeight + Style.px(24))
        padding: Style.px(12)
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }

        ColumnLayout {
            id: col
            anchors.fill: parent
            spacing: Style.px(8)
            Txt { text: qsTr("Running tasks"); font.pixelSize: Style.fontLead; font.family: Style.display }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: contentHeight
                clip: true
                spacing: Style.px(8)
                model: Tasks
                delegate: Rectangle {
                    required property int index
                    required property string title
                    required property string status
                    required property real progress
                    required property bool canCancel
                    width: ListView.view.width
                    height: Style.px(62)
                    radius: Style.radius
                    color: Style.alpha(Style.text, 0.05)
                    RowLayout {
                        anchors { fill: parent; margins: Style.px(10) }
                        spacing: Style.px(10)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(4)
                            RowLayout {
                                Layout.fillWidth: true
                                Txt { Layout.fillWidth: true; text: title; font.weight: Font.DemiBold }
                                Txt { text: root.pct(progress); muted: true; font.pixelSize: Style.fontSmall }
                            }
                            Txt { Layout.fillWidth: true; text: status; muted: true; font.pixelSize: Style.fontSmall; visible: status.length > 0 }
                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: progress >= 0 ? progress : 0
                                indeterminate: progress < 0
                            }
                        }
                        QuietButton {
                            iconName: "close"
                            tip: qsTr("Cancel")
                            plain: true
                            visible: canCancel
                            onClicked: Tasks.cancel(index)
                        }
                    }
                }
            }
        }
    }
}
