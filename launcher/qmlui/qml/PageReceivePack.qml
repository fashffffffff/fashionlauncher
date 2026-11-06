import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Receive a shared instance: Router.open("receive", { manifest }) or { link } or { file }, or paste a link here.
Item {
    id: page
    property var params: ({})
    property var manifest: null
    property string pasteError: ""
    property bool includeClient: true
    property var exclude: []
    property var matches: []
    property string mode: "new"          // existing | new | keep
    property string targetId: ""
    property string keepId: ""
    property bool applying: false

    readonly property var sameMatches: matches.filter(m => m.sameVersion)
    readonly property var shown: manifest ? Sharing.filterManifest(manifest, { includeClient: includeClient }) : ({})
    readonly property var chosen: manifest ? Sharing.filterManifest(manifest, { includeClient: includeClient, exclude: exclude }) : ({})
    readonly property var chosenItems: chosen.mods ? chosen.mods.concat(chosen.resourcepacks || [], chosen.shaderpacks || []) : []
    readonly property var localItems: chosenItems.filter(m => m.platform === "local")
    readonly property var target: sameMatches.find(m => m.instanceId === targetId) || null
    readonly property int missingCount: {
        if (!target) return 0
        const keys = (chosen.mods || []).map(m => m.key)
        return target.missing.filter(m => keys.indexOf(m.key) >= 0 && m.platform !== "local").length
    }
    readonly property int packCount: (chosen.resourcepacks || []).length + (chosen.shaderpacks || []).length

    function load(m) {
        if (!m || m.error) { pasteError = m ? m.error : ""; return }
        pasteError = ""
        manifest = m
        includeClient = m.includeClient !== false
        exclude = []
        matches = Sharing.matchInstances(m)
        newName.text = m.name || ""
        if (sameMatches.length) { mode = "existing"; targetId = sameMatches[0].instanceId }
        else { mode = "new"; targetId = "" }
        keepId = sameMatches.length ? sameMatches[0].instanceId : (matches.length ? matches[0].instanceId : "")
    }
    function loaderName(l) { return l ? l.charAt(0).toUpperCase() + l.slice(1) : "" }
    function apply() {
        Sharing.clearError()
        const opts = { includeClient: includeClient, exclude: exclude }
        if (mode === "existing") opts.targetInstanceId = targetId
        else if (mode === "keep") { opts.targetInstanceId = keepId; opts.keepAsIs = true }
        else opts.name = newName.text.trim()
        applying = Sharing.applyManifest(manifest, opts)
    }

    Component.onCompleted: {
        if (params.manifest) load(params.manifest)
        else if (params.link) load(Sharing.parseLink(params.link))
        else if (params.file) load(Sharing.openFile(params.file))
        else {
            const clip = Sharing.clipboardText()
            if (clip.indexOf("fashion://pack/") >= 0) linkField.text = clip.trim()
        }
    }

    Connections {
        target: Sharing
        function onApplied(id) {
            if (!page.applying) return
            page.applying = false
            const r = Sharing.result
            let msg = page.mode === "keep" ? qsTr("Using “%1” as it is").arg(Sharing.instanceName(id))
                    : page.mode === "new" ? qsTr("“%1” is ready").arg(Sharing.instanceName(id))
                    : qsTr("Added %n item(s) to “%1”", "", r.added || 0).arg(Sharing.instanceName(id))
            const missing = (r.failed || []).length + (r.local || []).length
            if (missing) msg += " · " + qsTr("%n couldn't be downloaded", "", missing)
            Router.toast(msg, (r.failed || []).length > 0)
            Router.back()
            Router.openInstance(id, page.mode === "keep" ? "overview" : "mods")
        }
        function onBusyChanged() { if (!Sharing.busy && Sharing.error) page.applying = false }
    }

    component Choice: AbstractButton {
        id: ch
        property string title
        property string description
        property bool selected: false
        default property alias extra: slot.data
        Layout.fillWidth: true
        padding: Style.px(14)
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.RadioButton
        Accessible.name: title
        Accessible.checked: selected
        background: Rectangle {
            radius: Style.radius
            color: ch.selected ? Style.alpha(Style.accent, 0.12) : ch.hovered ? Style.alpha(Style.text, 0.05) : Style.alpha(Style.surface, 0.5)
            border.width: ch.selected || ch.visualFocus ? 2 : 1
            border.color: ch.selected || ch.visualFocus ? Style.accent : Style.line
        }
        contentItem: RowLayout {
            spacing: Style.px(14)
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: Style.px(20); implicitHeight: Style.px(20); radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: ch.selected ? Style.accent : Style.alpha(Style.text, 0.35)
                Rectangle { anchors.centerIn: parent; visible: ch.selected; width: Style.px(10); height: width; radius: width / 2; color: Style.accent }
            }
            ColumnLayout {
                id: col
                Layout.fillWidth: true
                spacing: Style.px(4)
                Txt { Layout.fillWidth: true; text: ch.title; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                Txt { Layout.fillWidth: true; text: ch.description; muted: true; wrapMode: Text.WordWrap; font.pixelSize: Style.fontSmall; visible: text.length > 0 }
                ColumnLayout { id: slot; Layout.fillWidth: true; Layout.topMargin: children.length && ch.selected ? Style.px(6) : 0; visible: ch.selected }
            }
        }
    }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(16)

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            QuietButton { plain: true; iconName: "arrow-left"; tip: qsTr("Back"); onClicked: Router.back() }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: page.manifest ? (page.manifest.name || qsTr("Shared pack")) : qsTr("Open a shared instance")
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.px(28)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Txt {
                    visible: !!page.manifest
                    muted: true
                    text: !page.manifest ? "" : [
                        qsTr("Minecraft %1").arg(page.manifest.mc),
                        page.manifest.loader ? page.loaderName(page.manifest.loader) + " " + page.manifest.loaderVersion : qsTr("no mod loader"),
                        qsTr("%n mod(s)", "", (page.manifest.mods || []).length)
                    ].join(" · ")
                }
            }
            QuietButton {
                visible: !!page.manifest && !Sharing.busy
                text: qsTr("Open another")
                onClicked: { page.manifest = null; linkField.text = ""; linkField.forceActiveFocus() }
            }
        }

        // ---- no pack yet: paste a link or open a file
        ColumnLayout {
            visible: !page.manifest
            Layout.fillWidth: true
            Layout.maximumWidth: Style.px(640)
            spacing: Style.px(12)
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: qsTr("Paste the link a friend sent you, or open the .fashpack file. You'll see what's inside before anything is downloaded.")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(10)
                Field {
                    id: linkField
                    Layout.fillWidth: true
                    iconName: "link"
                    placeholderText: qsTr("Paste a share link (fashion://pack/…)")
                    onAccepted: page.load(Sharing.parseLink(text))
                    onTextChanged: page.pasteError = ""
                }
                PrimaryButton {
                    implicitHeight: Style.px(40)
                    text: qsTr("Open")
                    enabled: linkField.text.trim().length > 0
                    onClicked: page.load(Sharing.parseLink(linkField.text))
                }
            }
            Txt { visible: page.pasteError.length > 0; text: page.pasteError; color: Style.danger; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            QuietButton {
                iconName: "folder"
                text: qsTr("Open a .fashpack file")
                onClicked: { const m = Sharing.openFile(); if (m.error || m.format) page.load(m) }
            }
        }
        Item { visible: !page.manifest; Layout.fillHeight: true }

        // ---- pack loaded
        RowLayout {
            visible: !!page.manifest
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(24)

            // left: what to install
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 5
                spacing: Style.px(10)
                Txt { text: qsTr("What to install"); font.weight: Font.DemiBold }
                SettingsSegmented {
                    visible: !page.manifest || page.manifest.includeClient !== false
                    options: [{ value: false, label: qsTr("Server-side mods only") }, { value: true, label: qsTr("Everything") }]
                    value: page.includeClient
                    onPicked: (v) => page.includeClient = v
                }
                Txt {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Style.fontSmall
                    muted: true
                    text: page.manifest && page.manifest.includeClient === false
                          ? qsTr("Your friend shared only the server-side mods — enough to join or host the same server.")
                          : page.includeClient ? qsTr("Everything your friend uses, including client-only extras and packs.")
                                               : qsTr("Just what's needed to play on the same server. Client-only extras like minimaps and shaders are skipped.")
                }
                ShareModList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    manifest: page.shown
                    exclude: page.exclude
                    onEdited: (e) => page.exclude = e
                }
                Txt {
                    visible: page.localItems.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Style.fontSmall
                    color: Style.danger
                    text: qsTr("Ask your friend for these files — they aren't on Modrinth or CurseForge: %1").arg(page.localItems.map(m => m.file || m.name).join(", "))
                }
            }

            // right: where to put it
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 4
                spacing: Style.px(10)
                Txt { text: qsTr("Where to put it"); font.weight: Font.DemiBold }

                Choice {
                    visible: page.sameMatches.length > 0
                    selected: page.mode === "existing"
                    onClicked: page.mode = "existing"
                    title: page.target ? qsTr("Add to “%1”").arg(page.target.name) : qsTr("Add to one of your instances")
                    description: !page.target ? ""
                        : page.missingCount === 0 && page.packCount === 0 ? qsTr("You already have a similar instance with all of these mods.")
                        : qsTr("You already have a similar instance — use it and add %n missing mod(s). Your own mods stay.", "", page.missingCount)
                    SettingsDropdown {
                        visible: page.sameMatches.length > 1
                        Layout.fillWidth: true
                        options: page.sameMatches.map(m => ({ value: m.instanceId, label: m.name }))
                        value: page.targetId
                        onPicked: (v) => page.targetId = v
                    }
                }
                Choice {
                    selected: page.mode === "new"
                    onClicked: { page.mode = "new"; newName.forceActiveFocus() }
                    title: qsTr("Create a new instance")
                    description: qsTr("Minecraft %1%2 with these mods, kept apart from your other games.")
                        .arg(page.manifest ? page.manifest.mc : "")
                        .arg(page.manifest && page.manifest.loader ? " · " + page.loaderName(page.manifest.loader) : "")
                    Field {
                        id: newName
                        Layout.fillWidth: true
                        placeholderText: qsTr("Name")
                    }
                }
                Choice {
                    visible: Sharing.instanceOptions().length > 0
                    selected: page.mode === "keep"
                    onClicked: page.mode = "keep"
                    title: qsTr("Use my own instance without changes")
                    description: qsTr("Nothing is downloaded. Pick this if you already set it up yourself.")
                    SettingsDropdown {
                        Layout.fillWidth: true
                        options: Sharing.instanceOptions()
                        value: page.keepId
                        onPicked: (v) => page.keepId = v
                    }
                }
                Txt {
                    visible: page.matches.length > 0 && page.sameMatches.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Style.fontSmall
                    muted: true
                    text: qsTr("“%1” has some of these mods but runs a different Minecraft version or loader, so a new instance is safer.").arg(page.matches.length ? page.matches[0].name : "")
                }
                Item { Layout.fillHeight: true }
            }
        }

        // Footer
        Rectangle {
            visible: !!page.manifest
            Layout.fillWidth: true
            implicitHeight: footer.implicitHeight + Style.px(28)
            radius: Style.radius + Style.px(4)
            color: Style.panel(0)
            border.width: 1
            border.color: Style.panelBorder
            RowLayout {
                id: footer
                anchors { fill: parent; margins: Style.px(14) }
                spacing: Style.px(12)
                Txt {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Sharing.error ? Style.danger : Style.textMuted
                    text: Sharing.error || (Sharing.busy ? Sharing.status
                          : page.mode === "keep" ? qsTr("Your instance stays exactly as it is.")
                          : qsTr("%n item(s) selected. Files come from Modrinth and CurseForge.", "", page.chosenItems.length - page.localItems.length))
                }
                QuietButton { visible: Sharing.busy; text: qsTr("Stop"); onClicked: Sharing.cancel() }
                PrimaryButton {
                    iconName: "check"
                    enabled: !Sharing.busy && (page.mode !== "existing" || !!page.target) && (page.mode !== "keep" || !!page.keepId)
                    progress: Sharing.busy ? Sharing.progress : undefined
                    text: Sharing.busy ? qsTr("Working…")
                        : page.mode === "keep" ? qsTr("Use this instance")
                        : page.mode === "new" ? qsTr("Create instance")
                        : page.missingCount + page.packCount > 0 ? qsTr("Add mods") : qsTr("Open instance")
                    onClicked: page.apply()
                }
            }
        }
    }
}
