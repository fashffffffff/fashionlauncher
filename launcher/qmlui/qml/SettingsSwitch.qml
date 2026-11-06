import QtQuick
import QtQuick.Layouts

// A labelled on/off setting row.
SettingsRow {
    id: ss
    property bool checked: false
    signal toggled(bool on)
    SettingsToggle {
        checked: ss.checked
        onToggled: (on) => ss.toggled(on)
        Accessible.name: ss.label
    }
}
