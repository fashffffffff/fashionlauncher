import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A labelled slider with an editable number next to it. Commits on release, on keyboard steps and on typing.
ColumnLayout {
    id: ms
    property string label
    property string description
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real value
    property string unit: ""
    /// formatted hint next to the number, e.g. "4 GB"
    property string hint: ""
    signal committed(real value)
    Layout.fillWidth: true
    spacing: Style.px(6)

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(12)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.px(2)
            Txt { text: ms.label; Layout.fillWidth: true }
            Txt {
                visible: ms.description.length > 0
                text: ms.description
                muted: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                font.pixelSize: Style.fontSmall
            }
        }
        Txt { visible: ms.hint.length > 0; text: ms.hint; muted: true }
        Field {
            id: num
            Layout.preferredWidth: Style.px(96)
            horizontalAlignment: TextInput.AlignRight
            text: Math.round(sl.value)
            validator: IntValidator { bottom: Math.round(ms.from); top: Math.round(ms.to) }
            Accessible.name: ms.label
            onEditingFinished: if (acceptableInput) ms.committed(parseInt(text))
        }
        Txt { visible: ms.unit.length > 0; text: ms.unit; muted: true }
    }
    Slider {
        id: sl
        Layout.fillWidth: true
        from: ms.from; to: ms.to; stepSize: ms.stepSize
        value: ms.value
        snapMode: Slider.SnapAlways
        Accessible.name: ms.label
        onMoved: if (!pressed) ms.committed(value)
        onPressedChanged: if (!pressed) ms.committed(value)
        background: Rectangle {
            x: sl.leftPadding; y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: sl.availableWidth; height: Style.px(4); radius: 2
            color: Style.line
            Rectangle { width: sl.visualPosition * parent.width; height: parent.height; radius: 2; color: Style.accent }
        }
        handle: Rectangle {
            x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: Style.px(18); height: width; radius: width / 2
            color: Style.text
            border.width: sl.visualFocus ? 3 : 0
            border.color: Style.accent
        }
    }
}
