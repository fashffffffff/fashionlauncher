import QtQuick
import QtQuick.Layouts

// A labelled text setting. Commits when you press Enter or leave the field.
SettingsRow {
    id: sf
    property string value
    property string placeholder
    property int fieldWidth: Style.px(240)
    property var validator: null
    signal committed(string value)
    Field {
        Layout.preferredWidth: sf.fieldWidth
        text: sf.value
        placeholderText: sf.placeholder
        validator: sf.validator
        Accessible.name: sf.label
        onEditingFinished: if (acceptableInput && text !== sf.value) sf.committed(text)
    }
}
