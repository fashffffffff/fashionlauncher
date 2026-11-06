import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Community themes and backgrounds: browse a static gallery list, preview, install with one click.
Item {
    id: page
    property var params: ({})
    property string kind: params.kind === "background" ? "background" : "theme"
    property var activeTags: []

    readonly property var ofKind: Gallery.items.filter(i => i.type === kind)
    readonly property var tags: {
        const count = {}
        for (const i of ofKind) for (const t of i.tags) count[t] = (count[t] || 0) + 1
        return Object.keys(count).sort((a, b) => count[b] - count[a] || a.localeCompare(b)).slice(0, 14)
    }
    readonly property var shown: {
        const words = search.text.toLowerCase().split(/\s+/).filter(w => w)
        return ofKind.filter(i => {
            if (activeTags.some(t => i.tags.indexOf(t) < 0)) return false
            const hay = (i.name + " " + i.author + " " + i.description + " " + i.tags.join(" ")).toLowerCase()
            return words.every(w => hay.indexOf(w) >= 0)
        })
    }

    function toggleTag(t) {
        const a = activeTags.slice()
        const at = a.indexOf(t)
        if (at >= 0) a.splice(at, 1); else a.push(t)
        activeTags = a
    }
    function itemById(id) { return Gallery.items.find(i => i.id === id) }
    function openDetails(item) { if (!item) return; details.item = item; details.open() }
    function use(item) {
        const value = Gallery.installed[item.id]
        if (!value) return
        if (item.type === "theme") {
            Theme.apply(value)
        } else {
            Theme.setToken("background", "mode", "image")
            Theme.setToken("background", "image", value)
        }
        Sounds.play("success")
        Router.toast(item.type === "theme" ? qsTr("“%1” applied").arg(item.name) : qsTr("“%1” is your background now").arg(item.name), false)
    }

    onKindChanged: activeTags = []
    Component.onCompleted: {
        if (!Gallery.items.length || Gallery.error.length) Gallery.refresh()
        else Gallery.recheckInstalled()
    }

    Connections {
        target: Gallery
        function onInstallFinished(id, ok, kind, value, message) {
            const item = page.itemById(id)
            const name = item ? item.name : id
            if (!ok) {
                Sounds.play("error")
                Router.toast(message, true)
                return
            }
            if (kind === "theme") {
                Theme.reloadThemes()
            } else {
                const file = Backgrounds.importFile(value)
                if (!file.length) {
                    Sounds.play("error")
                    Router.toast(qsTr("Couldn't add “%1” to your backgrounds.").arg(name), true)
                    return
                }
                Gallery.markInstalled(id, file)
            }
            Sounds.play("success")
            Router.toast(kind === "theme" ? qsTr("“%1” installed. Press Apply to use it.").arg(name)
                                          : qsTr("“%1” added to your backgrounds.").arg(name), false)
        }
    }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(16)

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(16)
            QuietButton { plain: true; iconName: "arrow-left"; tip: qsTr("Back"); onClicked: Router.back() }
            Text {
                text: qsTr("Gallery")
                color: Style.text
                font.family: Style.display
                font.pixelSize: Style.px(30)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            CreateSegmented {
                options: [{ value: "theme", label: qsTr("Themes") }, { value: "background", label: qsTr("Backgrounds") }]
                value: page.kind
                onPicked: v => page.kind = v
            }
            QuietButton { iconName: "export"; text: qsTr("Share yours"); onClicked: share.open() }
            QuietButton {
                iconName: "refresh"; tip: qsTr("Refresh")
                enabled: !Gallery.loading
                onClicked: Gallery.refresh()
            }
        }

        // Search and tags
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            Field {
                id: search
                iconName: "search"
                placeholderText: page.kind === "theme" ? qsTr("Search themes") : qsTr("Search backgrounds")
                Layout.preferredWidth: Style.px(280)
                Keys.onEscapePressed: text = ""
            }
            Flow {
                Layout.fillWidth: true
                spacing: Style.px(6)
                Repeater {
                    model: page.tags
                    CreateChip {
                        text: modelData
                        checked: page.activeTags.indexOf(modelData) >= 0
                        onClicked: page.toggleTag(modelData)
                    }
                }
            }
        }

        // Shown when the online gallery isn't reachable
        Rectangle {
            visible: Gallery.usingSample && !Gallery.loading
            Layout.fillWidth: true
            implicitHeight: sampleRow.implicitHeight + Style.px(20)
            radius: Style.radius
            color: Style.panel(0)
            border.width: 1; border.color: Style.panelBorder
            RowLayout {
                id: sampleRow
                anchors { fill: parent; leftMargin: Style.px(14); rightMargin: Style.px(8) }
                spacing: Style.px(10)
                Icon { name: "globe"; size: Style.px(18); color: Style.textMuted }
                Txt {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    muted: true
                    text: qsTr("Couldn't reach the online gallery, so you're seeing the examples that come with the launcher.")
                }
                QuietButton { plain: true; text: qsTr("Try again"); onClicked: Gallery.refresh() }
            }
        }

        // Grid of cards
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                anchors.fill: parent
                clip: true
                visible: page.shown.length > 0
                readonly property int columns: Math.max(1, Math.floor(width / Style.px(280)))
                cellWidth: Math.floor(width / columns)
                cellHeight: cellWidth * 9 / 16 + Style.px(96)
                boundsBehavior: Flickable.StopAtBounds
                model: page.shown
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Item {
                    width: grid.cellWidth
                    height: grid.cellHeight
                    GalleryCard {
                        anchors { fill: parent; margins: Style.px(6) }
                        padding: Style.px(10)
                        item: modelData
                        onClicked: { Sounds.play("click"); page.openDetails(modelData) }
                        onInstall: Gallery.install(modelData.id)
                    }
                }
            }

            // Loading, errors, nothing found
            ColumnLayout {
                visible: page.shown.length === 0
                anchors.centerIn: parent
                width: Math.min(parent.width, Style.px(420))
                spacing: Style.px(12)
                BusyIndicator { visible: Gallery.loading; running: visible; Layout.alignment: Qt.AlignHCenter }
                Txt {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    muted: true
                    text: Gallery.loading ? qsTr("Loading the gallery…")
                        : Gallery.error.length ? qsTr("Couldn't load the gallery: %1").arg(Gallery.error)
                        : page.ofKind.length ? qsTr("Nothing matches your search.")
                        : page.kind === "theme" ? qsTr("No themes here yet.") : qsTr("No backgrounds here yet.")
                }
                QuietButton {
                    visible: !Gallery.loading && (Gallery.error.length > 0 || search.text.length > 0 || page.activeTags.length > 0)
                    Layout.alignment: Qt.AlignHCenter
                    text: Gallery.error.length ? qsTr("Try again") : qsTr("Clear search")
                    onClicked: {
                        if (Gallery.error.length) { Gallery.refresh(); return }
                        search.text = ""
                        page.activeTags = []
                    }
                }
            }
        }
    }

    GalleryDetails {
        id: details
        onInstall: Gallery.install(item.id)
        onUse: page.use(item)
    }

    // How to share a theme. Nothing is uploaded from the launcher.
    Popup {
        id: share
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(parent.width - Style.px(80), Style.px(560))
        modal: true
        padding: Style.px(24)
        onOpened: addressField.text = Gallery.indexUrl
        background: Rectangle { radius: Style.radius + Style.px(4); color: Style.surface; border.width: 1; border.color: Style.line }
        Overlay.modal: Rectangle { color: Style.alpha(Style.scrim, 0.6) }

        component Step: RowLayout {
            property int n
            property string text
            spacing: Style.px(12)
            Layout.fillWidth: true
            Rectangle {
                Layout.alignment: Qt.AlignTop
                width: Style.px(26); height: width; radius: width / 2
                color: Style.alpha(Style.accent, 0.2)
                Txt { anchors.centerIn: parent; text: parent.parent.n; font.weight: Font.DemiBold }
            }
            Txt { text: parent.text; wrapMode: Text.WordWrap; elide: Text.ElideNone; Layout.fillWidth: true }
        }

        contentItem: ColumnLayout {
            spacing: Style.px(14)
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Share your theme")
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                QuietButton { plain: true; iconName: "close"; tip: qsTr("Close"); onClicked: share.close() }
            }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                muted: true
                text: qsTr("The gallery is a public list on GitHub. Anyone can suggest a theme or a background there; once it's accepted, it shows up here for everyone.")
            }
            Step { n: 1; text: qsTr("Open your themes folder and find your theme's .json file.") }
            Step { n: 2; text: qsTr("Open the gallery page on GitHub and follow the steps there to add your file and a short description.") }
            Step { n: 3; text: qsTr("Only share things you made yourself or are allowed to share.") }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(10)
                QuietButton { iconName: "folder"; text: qsTr("Open themes folder"); onClicked: Theme.openThemesFolder() }
                QuietButton { iconName: "link"; text: qsTr("Open gallery page"); onClicked: Qt.openUrlExternally(Gallery.submitUrl) }
            }

            // Where the list comes from (for testers and other galleries)
            Txt { text: qsTr("Gallery address"); font.weight: Font.Medium; Layout.topMargin: Style.px(8) }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(8)
                Field {
                    id: addressField
                    Layout.fillWidth: true
                    placeholderText: Gallery.defaultIndexUrl
                    onAccepted: Gallery.indexUrl = text
                }
                QuietButton { text: qsTr("Use"); onClicked: Gallery.indexUrl = addressField.text }
                QuietButton {
                    visible: Gallery.indexUrl !== Gallery.defaultIndexUrl
                    text: qsTr("Reset")
                    onClicked: { Gallery.indexUrl = ""; addressField.text = Gallery.indexUrl }
                }
            }
        }
    }
}
