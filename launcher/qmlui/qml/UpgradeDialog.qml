import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Move an instance to another Minecraft version: pick the target, see what happens to every mod
// (replacement found / missing / unknown), then create an upgraded COPY — the original stays as it is.
// The loader stays the one the instance already uses.
Item {
    id: page
    property var params: ({})
    readonly property string instanceId: params.instanceId || ""
    readonly property var info: Launcher.versionInfo(instanceId)
    // The Instances row loads the profile lazily and updates, so the header shows the right version
    // even when the page is opened straight from a debug entry.
    readonly property var inst: { Instances.count; const i = Instances.indexOf(instanceId); return i >= 0 ? Instances.get(i) : null }
    readonly property string nowMc: (inst && inst.gameVersion) || (info.mc || "")
    readonly property string nowLoader: (inst && inst.loader)
                                        || ({ fabric: "Fabric", quilt: "Quilt", forge: "Forge", neoforge: "NeoForge" }[info.loader] || "")
    readonly property var plan: Upgrade.plan && Upgrade.plan.instanceId === instanceId ? Upgrade.plan : ({})
    readonly property bool planned: plan.state === "done"
    readonly property bool planning: Upgrade.planning
    readonly property var counts: planned ? plan.counts : {}
    readonly property string loaderLine: {
        const l = plan.loader || {}
        if (!l.key) return qsTr("No mod loader")
        return l.name + (l.currentVersion ? " " + l.currentVersion : "")
    }
    property string filter: ""  // "", "ready", "missing", "unknown"
    property var choices: ({})  // row id -> index of the chosen suggestion
    /// The version being moved to ("" = the picker is shown). Kept here, not in the bridge,
    /// so "Change version" can go back without touching a running or finished plan.
    property string targetMc: ""

    readonly property var filteredRows: {
        if (!planned) return []
        return plan.rows.filter(r => !filter.length || r.status === filter)
    }
    function nameOfStatus(s) {
        return s === "ready" ? qsTr("ready") : s === "missing" ? qsTr("missing") : qsTr("unknown")
    }
    function pick(version) {
        filter = ""
        choices = {}
        targetMc = version
        Upgrade.startPlan(instanceId, version)
    }
    function backToPicker() {
        targetMc = ""
        if (planning) Upgrade.cancelPlan()
    }

    property bool autoApply: false
    Component.onCompleted: {
        Creator.init()
        // Debug/automation entry (FFPRISM_PAGE=upgrade:<id>[:<mc>[:apply]]): plan (and apply) right away.
        if (params.planMc) page.pick(params.planMc)
        autoApply = !!params.autoApply
    }
    Connections {
        target: Upgrade
        function onPlanChanged() {
            if (page.autoApply && Upgrade.plan.state === "done") {
                page.autoApply = false
                Upgrade.apply(page.plan, "", {})
            }
        }
    }

    ColumnLayout {
        anchors { fill: parent; margins: Style.px(32) }
        spacing: Style.px(16)

        // ---- header
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            QuietButton { plain: true; iconName: "arrow-left"; tip: qsTr("Back"); onClicked: Router.back() }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: qsTr("Move to another Minecraft version")
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.px(28)
                    font.weight: Font.DemiBold
                }
                Txt {
                    muted: true
                    text: !page.nowMc ? qsTr("Pick the Minecraft version to move this instance to.")
                          : page.nowLoader ? qsTr("Now: Minecraft %1 with %2. The loader stays the same.").arg(page.nowMc).arg(page.nowLoader)
                          : qsTr("Now: Minecraft %1 without a mod loader.").arg(page.nowMc)
                }
            }
        }

        // ---- step 1: target version (hidden once a plan is shown; "Change" goes back to it)
        ColumnLayout {
            visible: page.targetMc.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(12)
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(8)
                Field {
                    id: mcSearch
                    Layout.fillWidth: true
                    iconName: "search"
                    placeholderText: qsTr("Search versions")
                    onTextChanged: Creator.versions.search = text
                    Keys.onEscapePressed: text = ""
                    Accessible.name: qsTr("Search Minecraft versions")
                }
                CreateChip { text: qsTr("Snapshots"); checked: Creator.versions.showSnapshots; onToggled: Creator.versions.showSnapshots = checked }
                CreateChip { text: qsTr("Old versions"); checked: Creator.versions.showOld; onToggled: Creator.versions.showOld = checked }
                QuietButton { iconName: "refresh"; tip: qsTr("Reload the version list"); onClicked: Creator.versions.reload(true) }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radius
                color: Style.panel(0)
                border.width: 1
                border.color: Style.panelBorder
                ListView {
                    id: mcList
                    anchors { fill: parent; margins: Style.px(6) }
                    clip: true
                    model: Creator.versions
                    spacing: Style.px(2)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: CreateListRow {
                        selected: page.planned && page.plan.targetMc === model.version
                        Accessible.name: model.version
                        onClicked: page.pick(model.version)
                        contentItem: RowLayout {
                            spacing: Style.px(10)
                            anchors { fill: parent; leftMargin: Style.px(12); rightMargin: Style.px(12) }
                            Txt { text: model.version; font.weight: Font.Medium; Layout.fillWidth: true }
                            CreateBadge { visible: model.recommended; text: qsTr("Recommended") }
                            CreateBadge { visible: model.type !== "release"; text: model.type; tint: Style.textMuted }
                            Txt { text: model.date; muted: true; font.pixelSize: Style.fontSmall }
                        }
                    }
                    // Empty / loading / failed state, like the version picker of the Create page
                    ColumnLayout {
                        visible: Creator.versions.count === 0
                        anchors.centerIn: parent
                        width: parent.width - Style.px(40)
                        spacing: Style.px(10)
                        BusyIndicator {
                            visible: Creator.versions.loading; running: visible
                            Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Style.px(36); Layout.preferredHeight: Style.px(36)
                        }
                        Txt {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Creator.versions.error.length ? Style.danger : Style.textMuted
                            text: Creator.versions.loading ? qsTr("Loading versions…")
                                  : Creator.versions.error.length ? qsTr("Couldn't load the list: %1").arg(Creator.versions.error)
                                  : mcSearch.text.length ? qsTr("No versions match “%1”.").arg(mcSearch.text)
                                  : qsTr("No versions to show.")
                        }
                        QuietButton {
                            visible: !Creator.versions.loading && Creator.versions.error.length > 0
                            text: qsTr("Try again"); iconName: "refresh"; Layout.alignment: Qt.AlignHCenter
                            onClicked: Creator.versions.reload(true)
                        }
                    }
                }
            }
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                muted: true
                text: qsTr("Pick the Minecraft version to move this instance to. Every mod is then checked against it.")
            }
        }

        // ---- step 1b: planning in progress
        ColumnLayout {
            visible: page.planning && page.targetMc.length > 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.px(12)
            BusyIndicator { running: visible; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Style.px(40); Layout.preferredHeight: Style.px(40) }
            Txt { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: page.plan.status || qsTr("Checking the mods…") }
            QuietButton { text: qsTr("Cancel"); Layout.alignment: Qt.AlignHCenter; onClicked: page.backToPicker() }
        }

        // ---- step 2: the plan table
        ColumnLayout {
            visible: page.planned && page.targetMc.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.px(12)

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.px(10)
                Txt {
                    Layout.fillWidth: true
                    font.weight: Font.DemiBold
                    text: planned ? qsTr("%1 ready · %2 missing · %3 unknown")
                                        .arg(page.counts.ready).arg(page.counts.missing).arg(page.counts.unknown) : ""
                }
                QuietButton { text: qsTr("Change version"); onClicked: page.backToPicker(); visible: !page.planning && !Upgrade.applying }
            }

            // loader banner
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: loaderTxt.implicitHeight + Style.px(20)
                radius: Style.radius
                color: Style.alpha((plan.loader && plan.loader.ok) ? Style.success : Style.danger, 0.12)
                border.width: 1
                border.color: (plan.loader && plan.loader.ok) ? Style.success : Style.danger
                visible: planned && (plan.loader || {}).key
                Txt {
                    id: loaderTxt
                    anchors { fill: parent; margins: Style.px(10) }
                    wrapMode: Text.WordWrap
                    text: {
                        const l = plan.loader || {}
                        if (!l.checked) return qsTr("%1 for Minecraft %2 could not be checked.").arg(l.name || "").arg(plan.targetMc || "")
                        const lv = (l.newVersion || l.currentVersion || "").trim()
                        return l.ok ? qsTr("%1 is available for Minecraft %2.").arg(lv.length ? l.name + " " + lv : l.name).arg(plan.targetMc)
                                    : qsTr("%1 is not available for Minecraft %2. Pick another version.").arg(l.name).arg(plan.targetMc)
                    }
                }
            }
            // one-way worlds warning
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: worldsTxt.implicitHeight + Style.px(20)
                radius: Style.radius
                color: Style.alpha(Style.danger, 0.12)
                border.width: 1
                border.color: Style.danger
                visible: planned && plan.delta === "upgrade" && plan.hasWorlds
                Txt {
                    id: worldsTxt
                    anchors { fill: parent; margins: Style.px(10) }
                    wrapMode: Text.WordWrap
                    text: qsTr("Worlds are copied as they are. Opening a world in a newer Minecraft version converts it permanently — it cannot be opened in the older version afterwards.")
                }
            }

            // filter chips
            RowLayout {
                spacing: Style.px(8)
                CreateChip { text: qsTr("All"); checked: page.filter.length === 0; onToggled: page.filter = "" }
                CreateChip { text: qsTr("Ready"); checked: page.filter === "ready"; onToggled: page.filter = checked ? "ready" : ""; visible: page.counts.ready > 0 }
                CreateChip { text: qsTr("Missing"); checked: page.filter === "missing"; onToggled: page.filter = checked ? "missing" : ""; visible: page.counts.missing > 0 }
                CreateChip { text: qsTr("Unknown"); checked: page.filter === "unknown"; onToggled: page.filter = checked ? "unknown" : ""; visible: page.counts.unknown > 0 }
            }

            // rows
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radius
                color: Style.panel(0)
                border.width: 1
                border.color: Style.panelBorder
                ListView {
                    id: rowsList
                    anchors { fill: parent; margins: Style.px(6) }
                    clip: true
                    spacing: Style.px(2)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    model: page.filteredRows
                    delegate: ColumnLayout {
                        width: rowsList.width
                        spacing: Style.px(4)
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: Style.px(52)
                            radius: Style.slotRadius
                            color: rowMouse.containsMouse ? Style.surfaceHi : "transparent"
                            MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                            RowLayout {
                                anchors { fill: parent; leftMargin: Style.px(10); rightMargin: Style.px(10) }
                                spacing: Style.px(10)
                                Rectangle {
                                    Layout.preferredWidth: Style.px(32); Layout.preferredHeight: Style.px(32)
                                    radius: Style.slotRadius
                                    color: Style.panel(1)
                                    Image {
                                        anchors.fill: parent
                                        source: modelData.icon || ""
                                        visible: !!modelData.icon
                                        fillMode: Image.PreserveAspectFit
                                        sourceSize: Qt.size(64, 64)
                                    }
                                    Icon { visible: !modelData.icon; anchors.centerIn: parent; name: "cube"; size: Style.px(18); color: Style.textMuted }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Txt { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideRight }
                                    Txt { Layout.fillWidth: true; text: modelData.fileName; muted: true; font.pixelSize: Style.fontSmall; elide: Text.ElideMiddle }
                                }
                                Txt {
                                    text: modelData.currentVersion || qsTr("Unknown version")
                                    muted: true
                                    font.pixelSize: Style.fontSmall
                                    Layout.maximumWidth: rowsList.width * 0.22
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignRight
                                }
                                Icon { name: "chevron-right"; size: Style.px(14); color: Style.textMuted; visible: modelData.status === "ready" }
                                Txt {
                                    text: modelData.targetVersion
                                    visible: modelData.status === "ready"
                                    font.pixelSize: Style.fontSmall
                                    font.weight: Font.Medium
                                    Layout.maximumWidth: rowsList.width * 0.22
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignRight
                                }
                                CreateBadge {
                                    text: page.nameOfStatus(modelData.status)
                                    tint: modelData.status === "ready" ? Style.success : modelData.status === "missing" ? Style.danger : Style.textMuted
                                }
                            }
                        }
                        // clearly-labelled suggestions for missing mods
                        Repeater {
                            model: modelData.status === "missing" ? modelData.suggestions : []
                            delegate: AbstractButton {
                                id: altRow
                                readonly property bool chosen: page.choices[modelData.id] === index
                                Layout.fillWidth: true
                                Layout.leftMargin: Style.px(36)
                                implicitHeight: altText.implicitHeight + Style.px(16)
                                onClicked: {
                                    const c = Object.assign({}, page.choices)
                                    if (chosen) delete c[modelData.id]
                                    else c[modelData.id] = index
                                    page.choices = c
                                }
                                background: Rectangle {
                                    radius: Style.slotRadius
                                    color: altRow.chosen ? Style.alpha(Style.accent, 0.16) : altRow.hovered ? Style.alpha(Style.text, 0.05) : "transparent"
                                    border.width: altRow.chosen ? 1 : 0
                                    border.color: Style.accent
                                }
                                contentItem: RowLayout {
                                    spacing: Style.px(8)
                                    Icon { name: altRow.chosen ? "check" : "plus"; size: Style.px(16); color: altRow.chosen ? Style.accent : Style.textMuted }
                                    Txt {
                                        id: altText
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: Style.fontSmall
                                        text: qsTr("Suggestion: %1 — %2").arg(modelData.name).arg(modelData.summary || modelData.version || "")
                                    }
                                }
                            }
                        }
                    }
                    Txt {
                        visible: page.filteredRows.length === 0
                        anchors.centerIn: parent
                        width: parent.width - Style.px(40)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        muted: true
                        text: qsTr("This instance has no mods. Only its Minecraft version and loader will be changed.")
                    }
                }
            }
        }

        // ---- footer
        Rectangle {
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
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(6)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(8)
                        Txt { text: qsTr("Name of the copy"); muted: true }
                        Field {
                            id: nameField
                            Layout.preferredWidth: Style.px(280)
                            text: page.planned ? Upgrade.defaultName(page.instanceId, page.plan.targetMc) : ""
                            placeholderText: qsTr("Name")
                            Accessible.name: qsTr("Name of the new instance")
                        }
                    }
                    Txt {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        muted: true
                        text: page.planned
                              ? qsTr("A copy named “%1” is created with the new version; the original instance stays untouched.").arg(nameField.text || "…")
                              : qsTr("The original instance stays untouched.")
                    }
                    RowLayout {
                        visible: Upgrade.applying
                        Layout.fillWidth: true
                        spacing: Style.px(8)
                        ProgressBar {
                            from: 0; to: 1
                            value: Upgrade.applyJob.progress >= 0 ? Upgrade.applyJob.progress : 0
                            indeterminate: Upgrade.applyJob.progress < 0
                            Layout.fillWidth: true
                        }
                        Txt { text: Upgrade.applyJob.status || qsTr("Working…"); muted: true; font.pixelSize: Style.fontSmall }
                    }
                }
                QuietButton {
                    text: Upgrade.applying ? qsTr("Cancel") : qsTr("Back")
                    onClicked: Upgrade.applying ? Upgrade.cancelApply() : (page.targetMc.length ? page.backToPicker() : Router.back())
                }
                PrimaryButton {
                    implicitHeight: Style.px(44)
                    text: qsTr("Create upgraded copy")
                    iconName: "copy"
                    enabled: page.planned && (page.plan.loader || {}).ok !== false && !page.planning && !Upgrade.applying
                    onClicked: Upgrade.apply(page.plan, nameField.text, page.choices)
                }
            }
        }

        // plan errors
        RowLayout {
            visible: plan.state === "error" || plan.state === "cancelled"
            Layout.fillWidth: true
            spacing: Style.px(10)
            Txt {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Style.danger
                text: plan.state === "error" ? (plan.error || qsTr("The check failed.")) : ""
            }
            QuietButton { text: qsTr("Pick another version"); onClicked: page.backToPicker() }
        }
    }

    // after the copy is created, jump to it
    Connections {
        target: Upgrade
        function onApplyFinished(ok, message, newName, newId) {
            if (ok && newId) {
                Router.back()
                Router.toastAction(message, qsTr("Open"), () => Router.openInstance(newId))
            } else if (!ok) {
                Router.toast(message, true)
            }
        }
    }
}
