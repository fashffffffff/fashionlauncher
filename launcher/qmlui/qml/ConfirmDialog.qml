import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one question dialog of the new UI. `buttons` go right to left in importance: the first is the
// main action (red when tone is "danger"), the last one is the way out. Emits chosen(index), -1 if dismissed.
//   confirm.ask({ title, text, buttons: [qsTr("Delete"), qsTr("Cancel")], tone: "danger" }, (i) => ...)
ModalCard {
    id: dlg
    property string text
    property var buttons: [qsTr("OK")]
    property string tone: "info"          // info | warning | danger
    property var callback: null
    property int result: -1
    signal chosen(int index)

    function ask(opts, cb) {
        title = opts.title || ""
        text = opts.text || ""
        buttons = opts.buttons && opts.buttons.length ? opts.buttons : [qsTr("OK")]
        tone = opts.tone || "info"
        callback = cb || null
        result = -1
        open()
    }
    function pick(i) { result = i; close() }
    onClosed: {
        const cb = callback
        callback = null
        chosen(result)
        if (cb) cb(result)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(12)
        Rectangle {
            visible: dlg.tone !== "info"
            Layout.alignment: Qt.AlignTop
            implicitWidth: Style.px(4); implicitHeight: Math.max(Style.px(24), msg.implicitHeight)
            radius: 2
            color: dlg.tone === "danger" ? Style.danger : Style.accent
        }
        Txt {
            id: msg
            Layout.fillWidth: true
            text: dlg.text
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            textFormat: Text.PlainText
            font.pixelSize: Style.fontBody
            lineHeight: 1.15
        }
    }
    RowLayout {
        Layout.alignment: Qt.AlignRight
        Layout.topMargin: Style.px(6)
        spacing: Style.px(8)
        Repeater {
            // Way out first (left), main action last (right)
            model: dlg.buttons.length > 1 ? dlg.buttons.slice(1).reverse() : []
            delegate: QuietButton { text: modelData; onClicked: dlg.pick(dlg.buttons.length - 1 - index) }
        }
        PrimaryButton {
            id: mainBtn
            text: dlg.buttons[0]
            implicitHeight: Style.px(40)
            fill: dlg.tone === "danger" ? Style.danger : Style.accent
            ink: dlg.tone === "danger" ? "#FFFFFF" : Style.onAccent
            onClicked: dlg.pick(0)
        }
    }
    onOpened: mainBtn.forceActiveFocus()
}
