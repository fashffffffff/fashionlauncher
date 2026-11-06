import QtQuick
import QtQuick.Layouts

// A titled card that groups related settings.
// With an instanceId and a group it reads/writes that instance's settings and shows an
// "Override global settings" switch; while it is off the controls show the global values, dimmed.
Rectangle {
    id: sec
    property string title
    property string description
    property string instanceId: ""
    property string group: ""          // "java" | "memory" | "window" | "behaviour"
    default property alias content: body.data

    readonly property bool overridable: instanceId.length > 0 && group.length > 0
    readonly property bool overridden: { Settings.revision; return overridable && Settings.instOverride(instanceId, group) }
    readonly property bool editable: !overridable || overridden

    // Value of a setting for this section: the instance's effective value, or the global one.
    function val(key) {
        Settings.revision
        return instanceId.length ? Settings.instGet(instanceId, key) : Settings.get(key)
    }
    function put(key, value) {
        if (instanceId.length) Settings.instSet(instanceId, key, value)
        else Settings.set(key, value)
    }

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + Style.px(40)
    radius: Style.radius + Style.px(2)
    color: Style.panel(1)
    border.width: 1
    border.color: Style.panelBorder

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(20) }
        spacing: Style.px(16)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(16)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.px(4)
                Text {
                    text: sec.title
                    color: Style.text
                    font.family: Style.ui
                    font.pixelSize: Style.fontLead
                    font.weight: Font.DemiBold
                }
                Txt {
                    visible: text.length > 0
                    Layout.fillWidth: true
                    text: sec.overridable && !sec.overridden
                          ? qsTr("Uses the launcher-wide settings. Turn on “Override” to set something different for this instance.")
                          : sec.description
                    muted: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Style.fontSmall
                }
            }
            SettingsToggle {
                visible: sec.overridable
                label: qsTr("Override")
                checked: sec.overridden
                onToggled: (on) => Settings.setInstOverride(sec.instanceId, sec.group, on)
                Layout.alignment: Qt.AlignTop
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: Style.px(16)
            enabled: sec.editable
            opacity: enabled ? 1 : 0.5
            Behavior on opacity { NumberAnimation { duration: Style.dur(120) } }
        }
    }
}
