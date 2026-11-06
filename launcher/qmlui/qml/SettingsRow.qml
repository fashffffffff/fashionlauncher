import QtQuick
import QtQuick.Layouts

// One setting: a label (and optional explanation) on the left, its control on the right.
RowLayout {
    id: row
    property string label
    property string description
    default property alias control: slot.data
    Layout.fillWidth: true
    spacing: Style.px(20)

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.px(2)
        Txt { text: row.label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Txt {
            visible: row.description.length > 0
            text: row.description
            muted: true
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSmall
        }
    }
    RowLayout {
        id: slot
        spacing: Style.px(8)
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }
}
