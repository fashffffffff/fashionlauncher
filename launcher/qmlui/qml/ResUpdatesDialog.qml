import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "What's new": every found update with its version change and the author's changelog, before updating.
Popup {
    id: dlg
    property var model            // InstalledModel
    property var updates: []
    property var expanded: ({})

    function show() { reload(); expanded = ({}); open() }
    function reload() { updates = model ? model.updateList() : [] }
    function toggle(i) { const e = Object.assign({}, expanded); e[i] = !e[i]; expanded = e }

    Connections {
        target: dlg.model
        function onUpdatesChanged() { if (dlg.opened) { dlg.reload(); if (dlg.updates.length === 0) dlg.close() } }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Style.px(640), parent ? parent.width - Style.px(48) : Style.px(640))
    height: Math.min(implicitHeight, parent ? parent.height - Style.px(64) : implicitHeight)
    modal: true
    focus: true
    padding: Style.px(24)
    background: Rectangle { radius: Style.radius; color: Style.surface; border.width: 1; border.color: Style.line }
    Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }

    contentItem: ColumnLayout {
        spacing: Style.px(14)

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("%n update(s) available", "", dlg.updates.length)
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
            }
            QuietButton { plain: true; iconName: "close"; tip: qsTr("Close"); onClicked: dlg.close() }
        }
        Txt { Layout.fillWidth: true; muted: true; wrapMode: Text.WordWrap; text: qsTr("See what changed before you update. Open a mod to read its changelog.") }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(contentHeight, Style.px(460))
            clip: true
            spacing: Style.px(8)
            model: dlg.updates
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index
                readonly property bool open: !!dlg.expanded[index]
                width: ListView.view.width - Style.px(10)
                height: col.implicitHeight + Style.px(20)
                radius: Style.radius
                color: Style.panel(0)
                border.width: 1
                border.color: card.open ? Style.alpha(Style.accent, 0.5) : Style.panelBorder

                ColumnLayout {
                    id: col
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(10); leftMargin: Style.px(14) }
                    spacing: Style.px(8)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(10)
                        Icon {
                            name: "chevron-right"
                            size: Style.px(14)
                            color: Style.textMuted
                            rotation: card.open ? 90 : 0
                            Behavior on rotation { NumberAnimation { duration: Style.dur(120) } }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.px(2)
                            Txt { Layout.fillWidth: true; text: modelData.name; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Txt {
                                Layout.fillWidth: true
                                muted: true
                                font.pixelSize: Style.fontSmall
                                elide: Text.ElideMiddle
                                text: (modelData.oldVersion ? modelData.oldVersion + "  →  " : "") + modelData.newVersion + "  ·  " + modelData.provider
                            }
                        }
                        QuietButton {
                            text: qsTr("Update")
                            enabled: dlg.model && dlg.model.busy === ""
                            onClicked: dlg.model.updateOne(modelData.index)
                        }
                    }

                    // The changelog
                    Rectangle {
                        visible: card.open
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(log.implicitHeight + Style.px(20), Style.px(240))
                        radius: Style.radius
                        color: Style.alpha(Style.text, 0.04)
                        Flickable {
                            id: logFlick
                            anchors { fill: parent; margins: Style.px(10) }
                            contentHeight: log.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: logFlick.contentHeight > logFlick.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                            Text {
                                id: log
                                width: logFlick.width - Style.px(12)
                                text: modelData.changelog.length ? modelData.changelog : qsTr("The author didn't write a changelog for this version.")
                                textFormat: !modelData.changelog.length ? Text.PlainText
                                          : modelData.changelogFormat === "html" ? Text.RichText : Text.MarkdownText
                                color: modelData.changelog.length ? Style.text : Style.textMuted
                                linkColor: Style.accent
                                font.family: Style.ui
                                font.pixelSize: Style.fontSmall
                                wrapMode: Text.Wrap
                                onLinkActivated: (url) => Qt.openUrlExternally(url)
                                HoverHandler { cursorShape: log.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                            }
                        }
                    }
                }
                TapHandler {
                    // tap the header to open/close the changelog
                    enabled: true
                    onTapped: (point) => { if (point.position.y < Style.px(56)) dlg.toggle(card.index) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Txt {
                Layout.fillWidth: true
                visible: dlg.model && dlg.model.busy === "updating"
                muted: true
                text: dlg.model ? (dlg.model.statusText || qsTr("Updating…")) : ""
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: true; visible: !(dlg.model && dlg.model.busy === "updating") }
            QuietButton { text: qsTr("Not now"); onClicked: dlg.close() }
            PrimaryButton {
                implicitHeight: Style.px(40)
                implicitWidth: Math.max(Style.px(150), implicitContentWidth + Style.px(40))
                text: dlg.model && dlg.model.busy === "updating" ? qsTr("Updating…") : qsTr("Update all (%1)").arg(dlg.updates.length)
                iconName: "export"
                enabled: dlg.model && dlg.model.busy === "" && dlg.updates.length > 0
                progress: dlg.model && dlg.model.busy === "updating" ? dlg.model.progress : undefined
                onClicked: dlg.model.updateAll()
            }
        }
    }
}
