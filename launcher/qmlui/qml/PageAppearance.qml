import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Theme picker and live editor. Every change is written to the theme's JSON file.
Item {
    id: page
    property var params: ({})

    component Heading: Text {
        color: Style.text
        font.family: Style.ui
        font.pixelSize: Style.fontLead
        font.weight: Font.DemiBold
        Layout.topMargin: Style.px(8)
    }

    component Segmented: Row {
        id: seg
        property var options: []      // [{ value, label }]
        property string value
        signal picked(string value)
        spacing: 0
        Repeater {
            model: seg.options
            delegate: AbstractButton {
                id: segBtn
                readonly property bool on: modelData.value === seg.value
                implicitHeight: Style.px(36)
                implicitWidth: segText.implicitWidth + Style.px(28)
                hoverEnabled: true
                Accessible.name: modelData.label
                onClicked: seg.picked(modelData.value)
                background: Rectangle {
                    radius: Style.radius - 2
                    color: segBtn.on ? Style.accent : segBtn.hovered ? Style.alpha(Style.text, 0.08) : "transparent"
                    border.width: segBtn.visualFocus ? 2 : 0
                    border.color: Style.text
                }
                contentItem: Text {
                    id: segText
                    text: modelData.label
                    color: segBtn.on ? Style.onAccent : Style.text
                    font.family: Style.ui
                    font.pixelSize: Style.fontBody
                    font.weight: segBtn.on ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    component TokenSlider: RowLayout {
        id: ts
        property string label
        property real from: 0
        property real to: 1
        property real stepSize: 0.01
        property real value
        property string suffix: ""
        property int decimals: 2
        signal moved(real value)
        spacing: Style.px(14)
        Layout.fillWidth: true
        Txt { text: ts.label; Layout.preferredWidth: Style.px(150) }
        Slider {
            id: sl
            Layout.fillWidth: true
            from: ts.from; to: ts.to; stepSize: ts.stepSize
            value: ts.value
            onMoved: ts.moved(value)
            Accessible.name: ts.label
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
        Txt {
            text: ts.value.toFixed(ts.decimals) + ts.suffix
            muted: true
            Layout.preferredWidth: Style.px(56)
            horizontalAlignment: Text.AlignRight
        }
    }

    component ColorRow: RowLayout {
        id: cr
        property string key
        property string label
        spacing: Style.px(12)
        Layout.fillWidth: true
        Rectangle {
            width: Style.px(28); height: width; radius: Style.px(6)
            color: Theme.colors[cr.key] || "transparent"
            border.width: 1; border.color: Style.alpha(Style.text, 0.2)
        }
        Txt { text: cr.label; Layout.fillWidth: true }
        Field {
            id: hex
            Layout.preferredWidth: Style.px(120)
            text: Theme.colors[cr.key] || ""
            font.family: "Menlo"
            validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?/ }
            onEditingFinished: if (acceptableInput) Theme.setToken("colors", cr.key, text)
        }
    }

    RowLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(40)

        // Theme list
        ColumnLayout {
            Layout.preferredWidth: Style.px(300)
            Layout.fillHeight: true
            spacing: Style.px(14)
            Text {
                text: qsTr("Appearance")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(34)
                font.weight: Font.DemiBold
            }
            ListView {
                id: themeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Style.px(8)
                model: Theme.themes
                boundsBehavior: Flickable.StopAtBounds
                delegate: AbstractButton {
                    id: themeCard
                    width: themeList.width
                    height: Style.px(64)
                    hoverEnabled: true
                    readonly property bool active: modelData.id === Theme.themeId
                    Accessible.name: modelData.name
                    onClicked: Theme.apply(modelData.id)
                    background: Rectangle {
                        radius: Style.radius
                        color: themeCard.hovered ? Style.alpha(Style.surfaceHi, 0.9) : Style.alpha(Style.surface, 0.9)
                        border.width: themeCard.active ? 2 : 1
                        border.color: themeCard.active ? Style.accent : Style.alpha(Style.text, 0.07)
                    }
                    contentItem: RowLayout {
                        anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                        spacing: Style.px(12)
                        // a tiny preview of the theme: its background with an accent block on it
                        Rectangle {
                            width: Style.px(44); height: Style.px(40); radius: Style.px(6)
                            color: modelData.bg
                            border.width: 1; border.color: Style.alpha(Style.text, 0.15)
                            Rectangle { x: Style.px(6); y: Style.px(22); width: Style.px(14); height: Style.px(12); radius: 2; color: modelData.accent }
                            Rectangle { x: Style.px(22); y: Style.px(22); width: Style.px(14); height: Style.px(12); radius: 2; color: modelData.surface }
                            Rectangle { x: Style.px(6); y: Style.px(7); width: Style.px(26); height: Style.px(4); radius: 2; color: modelData.text }
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Txt { text: modelData.name; font.weight: Font.Medium; Layout.fillWidth: true }
                            Txt {
                                text: modelData.builtin ? qsTr("Built-in") : (modelData.author ? qsTr("By %1").arg(modelData.author) : qsTr("Yours"))
                                muted: true
                                font.pixelSize: Style.fontSmall
                            }
                        }
                        Icon { visible: themeCard.active; name: "check"; color: Style.accent; size: Style.px(18) }
                    }
                }
            }
            QuietButton {
                Layout.fillWidth: true
                iconName: "folder"
                text: qsTr("Open themes folder")
                onClicked: Theme.openThemesFolder()
            }
            Txt {
                Layout.fillWidth: true
                text: qsTr("Put .json theme files in this folder to install them. Edits to a theme file show up here as soon as you save it.")
                muted: true
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSmall
            }
        }

        // Editor
        ScrollView {
            id: editorScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: Math.min(editorScroll.availableWidth, Style.px(640))
                spacing: Style.px(14)

                Rectangle {
                    Layout.fillWidth: true
                    visible: Theme.lastError.length > 0
                    implicitHeight: errText.implicitHeight + Style.px(20)
                    radius: Style.radius
                    color: Style.alpha(Style.danger, 0.15)
                    border.width: 1; border.color: Style.danger
                    Txt {
                        id: errText
                        anchors { fill: parent; margins: Style.px(10) }
                        text: qsTr("Your theme couldn't be loaded, so the default is shown. %1").arg(Theme.lastError)
                        wrapMode: Text.WordWrap
                    }
                }

                Heading { text: qsTr("Theme name") }
                Field {
                    id: nameField
                    Layout.fillWidth: true
                    text: Theme.name
                    onEditingFinished: if (text !== Theme.name) Theme.rename(text)
                }
                Txt {
                    visible: Theme.builtin
                    Layout.fillWidth: true
                    text: qsTr("Built-in themes stay as they are. Your first change saves a copy you can edit freely.")
                    muted: true
                    wrapMode: Text.WordWrap
                }

                Heading { text: qsTr("Accent") }
                Flow {
                    Layout.fillWidth: true
                    spacing: Style.px(8)
                    Repeater {
                        // [accent, readable text on it]
                        model: [["#F5A524", "#1F1606"], ["#E5484D", "#FFFFFF"], ["#FF8DC7", "#2A0E1C"], ["#B48CF2", "#170D2B"],
                                ["#4C8DFF", "#FFFFFF"], ["#3DD6C6", "#06211E"], ["#7BD448", "#11200A"], ["#ECEEF2", "#1E2128"]]
                        delegate: AbstractButton {
                            id: sw
                            width: Style.px(36); height: width
                            hoverEnabled: true
                            Accessible.name: modelData[0]
                            onClicked: { Theme.setToken("colors", "accent", modelData[0]); Theme.setToken("colors", "onAccent", modelData[1]) }
                            background: Rectangle {
                                radius: width / 2
                                color: modelData[0]
                                border.width: Qt.colorEqual(Style.accent, modelData[0]) || sw.visualFocus ? 3 : 0
                                border.color: Style.text
                            }
                        }
                    }
                }

                Heading { text: qsTr("Background") }
                Segmented {
                    value: Style.bgCfg.mode || "screenshot"
                    options: [{ value: "screenshot", label: qsTr("Screenshot") }, { value: "image", label: qsTr("Image") },
                              { value: "pattern", label: qsTr("Blocks") }, { value: "solid", label: qsTr("Solid") }]
                    onPicked: (v) => { if (v === "image" && !Style.bgCfg.image) imageDialog.open(); else Theme.setToken("background", "mode", v) }
                }
                Txt {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    muted: true
                    text: (Style.bgCfg.mode || "screenshot") === "screenshot"
                        ? qsTr("Shows the newest screenshot of the selected instance. Instances without screenshots get a block pattern.")
                        : (Style.bgCfg.mode === "image" ? qsTr("The same picture behind every instance.") : "")
                    visible: text.length > 0
                }
                QuietButton {
                    visible: Style.bgCfg.mode === "image"
                    iconName: "image"
                    text: Style.bgCfg.image ? qsTr("Change image") : qsTr("Choose image")
                    onClicked: imageDialog.open()
                }
                TokenSlider {
                    label: qsTr("Blur"); from: 0; to: 64; stepSize: 1; decimals: 0
                    value: Style.bgCfg.blur || 0
                    onMoved: (v) => Theme.setToken("background", "blur", Math.round(v))
                }
                TokenSlider {
                    label: qsTr("Darken"); from: 0; to: 0.9
                    value: Style.bgCfg.dim || 0
                    onMoved: (v) => Theme.setToken("background", "dim", Math.round(v * 100) / 100)
                }

                Heading { text: qsTr("Shape and size") }
                TokenSlider {
                    label: qsTr("Corner radius"); from: 0; to: 20; stepSize: 1; decimals: 0; suffix: " px"
                    value: Style.radius
                    onMoved: (v) => { Theme.setToken("shape", "radius", Math.round(v)); Theme.setToken("shape", "slotRadius", Math.max(0, Math.round(v) - 2)) }
                }
                TokenSlider {
                    label: qsTr("Hotbar slots"); from: 52; to: 96; stepSize: 2; decimals: 0; suffix: " px"
                    value: Style.slotSize
                    onMoved: (v) => Theme.setToken("shape", "slotSize", Math.round(v))
                }
                TokenSlider {
                    label: qsTr("Interface scale"); from: 0.85; to: 1.3; stepSize: 0.05
                    value: Style.scale
                    onMoved: (v) => Theme.setToken("type", "scale", Math.round(v * 100) / 100)
                }

                Heading { text: qsTr("Layout") }
                RowLayout {
                    spacing: Style.px(14)
                    Txt { text: qsTr("Menu position"); Layout.preferredWidth: Style.px(150) }
                    Segmented {
                        value: Style.navPosition
                        options: [{ value: "left", label: qsTr("Left") }, { value: "right", label: qsTr("Right") }]
                        onPicked: (v) => Theme.setToken("layout", "navPosition", v)
                    }
                }
                RowLayout {
                    spacing: Style.px(14)
                    Txt { text: qsTr("Menu style"); Layout.preferredWidth: Style.px(150) }
                    Segmented {
                        value: Style.navCompact ? "compact" : "full"
                        options: [{ value: "full", label: qsTr("Icons and names") }, { value: "compact", label: qsTr("Icons only") }]
                        onPicked: (v) => Theme.setToken("layout", "navStyle", v)
                    }
                }
                RowLayout {
                    spacing: Style.px(14)
                    Txt { text: qsTr("Panels"); Layout.preferredWidth: Style.px(150) }
                    Segmented {
                        value: Style.panelStyle
                        options: [{ value: "glass", label: qsTr("Glass") }, { value: "solid", label: qsTr("Solid") },
                                  { value: "outline", label: qsTr("Outline") }]
                        onPicked: (v) => Theme.setToken("layout", "panelStyle", v)
                    }
                }
                TokenSlider {
                    visible: Style.panelStyle === "glass"
                    label: qsTr("Panel opacity"); from: 0.4; to: 1
                    value: Style.panelOpacity
                    onMoved: (v) => Theme.setToken("layout", "panelOpacity", Math.round(v * 100) / 100)
                }
                TokenSlider {
                    label: qsTr("Title size"); from: 36; to: 76; stepSize: 2; decimals: 0; suffix: " px"
                    value: Theme.layout.heroSize || 52
                    onMoved: (v) => Theme.setToken("layout", "heroSize", Math.round(v))
                }
                RowLayout {
                    spacing: Style.px(12)
                    Switch {
                        id: hotbarSwitch
                        checked: Style.showHotbar
                        onToggled: Theme.setToken("layout", "showHotbar", checked)
                        Accessible.name: qsTr("Show hotbar on Home")
                        indicator: Rectangle {
                            implicitWidth: Style.px(42); implicitHeight: Style.px(24); radius: height / 2
                            color: hotbarSwitch.checked ? Style.accent : Style.line
                            border.width: hotbarSwitch.visualFocus ? 2 : 0
                            border.color: Style.text
                            Rectangle {
                                width: Style.px(18); height: width; radius: width / 2
                                y: (parent.height - height) / 2
                                x: hotbarSwitch.checked ? parent.width - width - 3 : 3
                                color: hotbarSwitch.checked ? Style.onAccent : Style.text
                                Behavior on x { NumberAnimation { duration: Style.dur(120) } }
                            }
                        }
                    }
                    Txt { text: qsTr("Show hotbar on Home") }
                }

                Heading { text: qsTr("Fonts") }
                FontPicker {
                    label: qsTr("Titles")
                    value: Style.display
                    onPicked: (f) => Theme.setToken("type", "display", f)
                }
                FontPicker {
                    label: qsTr("Interface")
                    value: Style.ui
                    onPicked: (f) => Theme.setToken("type", "ui", f)
                }

                Heading { text: qsTr("Motion") }
                RowLayout {
                    spacing: Style.px(12)
                    Switch {
                        id: motionSwitch
                        checked: Style.motion
                        onToggled: Theme.setToken("motion", "enabled", checked)
                        Accessible.name: qsTr("Animations")
                        indicator: Rectangle {
                            implicitWidth: Style.px(42); implicitHeight: Style.px(24); radius: height / 2
                            color: motionSwitch.checked ? Style.accent : Style.line
                            border.width: motionSwitch.visualFocus ? 2 : 0
                            border.color: Style.text
                            Rectangle {
                                width: Style.px(18); height: width; radius: width / 2
                                y: (parent.height - height) / 2
                                x: motionSwitch.checked ? parent.width - width - 3 : 3
                                color: motionSwitch.checked ? Style.onAccent : Style.text
                                Behavior on x { NumberAnimation { duration: Style.dur(120) } }
                            }
                        }
                    }
                    Txt { text: qsTr("Animations") }
                }

                Heading { text: qsTr("All colors") }
                ColorRow { key: "bg"; label: qsTr("Background") }
                ColorRow { key: "surface"; label: qsTr("Panels") }
                ColorRow { key: "surfaceHi"; label: qsTr("Raised panels") }
                ColorRow { key: "line"; label: qsTr("Borders") }
                ColorRow { key: "text"; label: qsTr("Text") }
                ColorRow { key: "textMuted"; label: qsTr("Secondary text") }
                ColorRow { key: "accent"; label: qsTr("Accent") }
                ColorRow { key: "onAccent"; label: qsTr("Text on accent") }
                ColorRow { key: "danger"; label: qsTr("Danger") }
                ColorRow { key: "success"; label: qsTr("Success") }
                ColorRow { key: "scrim"; label: qsTr("Background shade") }

                RowLayout {
                    Layout.topMargin: Style.px(12)
                    spacing: Style.px(10)
                    QuietButton { iconName: "copy"; text: qsTr("Duplicate theme"); onClicked: Theme.duplicate(qsTr("%1 copy").arg(Theme.name)) }
                    QuietButton {
                        visible: !Theme.builtin
                        iconName: "trash"; danger: true
                        text: qsTr("Delete theme")
                        onClicked: Theme.remove(Theme.themeId)
                    }
                }
                Item { Layout.preferredHeight: Style.px(24) }
            }
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Choose a background image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: Theme.setBackgroundImage(selectedFile)
    }
}
