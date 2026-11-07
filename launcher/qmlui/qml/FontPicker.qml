import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pick any installed font; the list previews each family in its own face.
RowLayout {
    id: fp
    property string label
    property string value
    signal picked(string family)
    spacing: Style.px(14)
    Layout.fillWidth: true

    Txt { text: fp.label; Layout.preferredWidth: Style.px(150) }
    QuietButton {
        Layout.fillWidth: true
        text: fp.value
        onClicked: { filter.text = ""; pop.open(); filter.forceActiveFocus() }
        contentItem: Text {
            text: fp.value
            color: Style.text
            font.family: fp.value
            font.pixelSize: Style.fontBody + 1
            leftPadding: Style.px(12)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    Popup {
        id: pop
        y: -height / 2
        width: Style.px(360)
        height: Style.px(420)
        padding: Style.px(10)
        modal: true
        background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
        contentItem: ColumnLayout {
            spacing: Style.px(8)
            Field { id: filter; Layout.fillWidth: true; iconName: "search"; placeholderText: qsTr("Search fonts") }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                // Bundled faces first, then everything installed on this computer
                model: {
                    const bundled = ["Dela Gothic One", "Tektur", "Golos Text", "Unbounded"]
                    const all = bundled.concat(Qt.fontFamilies().filter(f => bundled.indexOf(f) < 0 && !f.startsWith(".")))
                    const q = filter.text.toLowerCase()
                    return q ? all.filter(f => f.toLowerCase().indexOf(q) >= 0) : all
                }
                delegate: AbstractButton {
                    id: row
                    width: ListView.view.width - Style.px(8)
                    height: Style.px(36)
                    hoverEnabled: true
                    onClicked: { fp.picked(modelData); pop.close() }
                    background: Rectangle {
                        radius: Style.radius - 2
                        color: modelData === fp.value ? Style.alpha(Style.accent, 0.18) : row.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
                    }
                    contentItem: Text {
                        text: modelData
                        color: Style.text
                        font.family: modelData
                        font.pixelSize: Style.fontBody + 1
                        leftPadding: Style.px(10)
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
