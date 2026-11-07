import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Crash doctor: why the game stopped. Shows a cause only when the log states it outright (the mod loader's
// own dependency check, out of memory, wrong Java, graphics driver, a damaged mod file), always with the log
// lines that say so. Otherwise it says plainly that the cause is unknown and shows where the error starts.
ModalCard {
    id: dlg
    readonly property var report: Doctor.report
    readonly property string instanceId: report.instanceId || ""
    readonly property var findings: report.findings || []
    readonly property bool known: findings.length > 0
    readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : Qt.platform.os === "osx" ? "Menlo" : "monospace"
    // key -> { state: "working" | "done" | "failed", message }
    property var fixState: ({})
    property bool showExcerpt: false
    property bool showMentioned: false
    property bool confirmShare: false

    width: Math.min(parent ? parent.width - Style.px(48) : 680, Style.px(680))
    height: Math.min(parent ? parent.height - Style.px(48) : 640, Math.max(Style.px(360), implicitHeight))
    title: known ? qsTr("Why “%1” stopped").arg(report.instanceName || "")
                 : qsTr("“%1” stopped unexpectedly").arg(report.instanceName || "")
    subtitle: known ? qsTr("The log names the cause directly. The lines it comes from are quoted below.")
                    : qsTr("Couldn't determine the exact cause. Below is the part of the log where the error starts — it helps whoever looks into it.")

    onAboutToShow: { fixState = ({}); showExcerpt = false; showMentioned = false; confirmShare = false; Launcher.resetLogShare() }

    function setFix(key, state, message) {
        const s = Object.assign({}, fixState)
        s[key] = { state: state, message: message || "" }
        fixState = s
    }
    Connections {
        target: Doctor
        function onFixFinished(key, ok, message) { dlg.setFix(key, ok ? "done" : "failed", message) }
    }

    // ---- wording of a finding
    function gb(mb) {
        const v = Math.round(mb / 1024 * 10) / 10
        return v.toLocaleString(Qt.locale(), "f", Number.isInteger(v) ? 0 : 1)
    }
    function headline(f) {
        const dep = f.dependencyName || f.dependency
        switch (f.kind) {
        case "missing-dependency": return qsTr("%1 needs %2, which isn't installed").arg(f.mod).arg(dep)
        case "wrong-dependency":
            return f.dependency === "minecraft" ? qsTr("%1 is made for a different Minecraft version").arg(f.mod)
                                                : qsTr("%1 needs a different version of %2").arg(f.mod).arg(dep)
        case "incompatible": return qsTr("%1 refuses to load together with %2").arg(f.mod).arg(dep)
        case "duplicate": return f.dependency ? qsTr("%1 is installed more than once").arg(f.dependency)
                                              : qsTr("A mod is installed more than once")
        case "memory": return qsTr("The game ran out of memory")
        case "memory-reserve": return qsTr("Java couldn't get the memory it was given")
        case "memory-system": return qsTr("The computer ran out of memory for Java")
        case "java": return f.javaTooNew ? qsTr("This version only runs on Java %1").arg(f.javaMajor)
                                         : qsTr("Java %1 or newer is required").arg(f.javaMajor)
        case "graphics": return qsTr("The graphics driver can't run Minecraft")
        case "corrupt-file": return qsTr("The file %1 is damaged").arg(f.files[0])
        }
        return ""
    }
    function detail(f) {
        switch (f.kind) {
        case "missing-dependency": return qsTr("Required: %1.").arg(f.range || qsTr("any version"))
        case "wrong-dependency": return qsTr("Required: %1. Installed: %2.").arg(f.range).arg(f.found)
        case "incompatible": return qsTr("Its author marked this combination as broken, so the mod loader stopped. Remove one of them or look for updates.")
        case "duplicate": return f.files.length ? qsTr("Keep one of: %1.").arg(f.files.join(", ")) : qsTr("Keep only one copy in the mods folder.")
        case "memory": {
            const cur = Settings.instGet(instanceId, "MaxMemAlloc"), rec = Settings.recommendedMaxMemory(instanceId)
            return qsTr("Max memory now: %1 GB. Recommended for this instance on this computer: %2 GB.").arg(gb(cur)).arg(gb(rec))
        }
        case "memory-reserve": {
            const cur = Settings.instGet(instanceId, "MaxMemAlloc")
            return qsTr("Max memory is %1 GB — more than this computer, or a 32-bit Java, can provide.").arg(gb(cur))
        }
        case "memory-system": return qsTr("Close other programs, or give the game less memory so the system keeps enough.")
        case "java":
            return f.javaCurrent > 0 ? qsTr("The game was started with Java %1.").arg(f.javaCurrent)
                                     : qsTr("The Java the game was started with is too old for it.")
        case "graphics": return qsTr("Install the latest driver from the maker of your graphics card. On laptops with two GPUs, run the game on the dedicated one.")
        case "corrupt-file": return f.fileExists === false ? qsTr("It is no longer in the mods folder.")
                                                            : qsTr("It can't be opened as an archive — usually a download that was cut off.")
        }
        return ""
    }

    // ---- fixes
    function memoryTarget(f) {
        const cur = Settings.instGet(instanceId, "MaxMemAlloc"), rec = Settings.recommendedMaxMemory(instanceId)
        if (f.kind === "memory") {
            if (cur >= rec) return 0
            const want = Math.ceil(Math.max(cur + 1024, cur * 1.5) / 512) * 512
            return Math.min(rec, want)
        }
        return cur > rec ? rec : 0      // reserve / system: give less
    }
    function pickJava(f) {
        const list = Settings.javaInstalls
        let best = null
        for (const j of list) {
            if (j.major === f.javaMajor && (!best || best.major !== f.javaMajor)) best = j
            else if (!f.javaTooNew && j.major > f.javaMajor && (!best || (best.major !== f.javaMajor && j.major < best.major))) best = j
        }
        return best
    }
    function fixLabel(f) {
        switch (f.fix) {
        case "install": return qsTr("Install %1").arg(f.dependency)
        case "mods": return qsTr("Open mods")
        case "memory": {
            const t = memoryTarget(f)
            return t ? (f.kind === "memory" ? qsTr("Raise to %1 GB") : qsTr("Lower to %1 GB")).arg(gb(t)) : ""
        }
        case "java": {
            const j = pickJava(f)
            return j ? qsTr("Use Java %1").arg(j.version) : qsTr("Download Java %1").arg(f.javaMajor)
        }
        case "reinstall": return qsTr("Download again")
        case "trash": return qsTr("Move to Trash")
        }
        return ""
    }
    function fixNote(f) {
        if (f.fix === "memory" && !memoryTarget(f))
            return f.kind === "memory" ? qsTr("Max memory is already at or above the amount recommended for this computer. Remove some heavy mods or close other programs.") : ""
        return ""
    }
    function fixKey(f) { return f.fix + ":" + (f.dependency || (f.files && f.files[0]) || f.kind) }
    function runFix(f) {
        const key = fixKey(f)
        switch (f.fix) {
        case "install":
            setFix(key, "working")
            Doctor.installDependency(instanceId, f.dependency)
            break
        case "mods":
            dlg.close()
            Router.openInstance(instanceId, "mods")
            break
        case "memory": {
            const t = memoryTarget(f)
            if (!Settings.instOverride(instanceId, "memory")) Settings.setInstOverride(instanceId, "memory", true)
            Settings.instSet(instanceId, "MaxMemAlloc", t)
            if (Settings.instGet(instanceId, "MinMemAlloc") > t) Settings.instSet(instanceId, "MinMemAlloc", t)
            setFix(key, "done", qsTr("Max memory is now %1 GB.").arg(gb(t)))
            break
        }
        case "java": {
            const j = pickJava(f)
            if (j) {
                if (!Settings.instOverride(instanceId, "java")) Settings.setInstOverride(instanceId, "java", true)
                Settings.instSet(instanceId, "JavaPath", j.path)
                setFix(key, "done", qsTr("This instance now uses Java %1.").arg(j.version))
            } else {
                dlg.close()
                Settings.downloadJava(instanceId)
            }
            break
        }
        case "reinstall":
            setFix(key, "working")
            Doctor.reinstallFile(instanceId, f.files[0])
            break
        case "trash":
            if (Doctor.trashFile(instanceId, f.files[0])) setFix(key, "done", qsTr("Moved to the Trash. Install the mod again from the mods tab."))
            else setFix(key, "failed", qsTr("Couldn't move the file."))
            break
        }
    }

    function copyAll() {
        let out = []
        out.push(title)
        for (const f of findings) {
            out.push("", "• " + headline(f), "  " + detail(f))
            for (const e of f.evidence) out.push("    > " + e)
        }
        if (report.excerpt) out.push("", report.excerpt)
        Doctor.copyText(out.join("\n"))
        Router.toast(qsTr("Copied."), false)
    }

    Component.onCompleted: if (Settings.javaInstalls.length === 0) Settings.scanJava()

    // ---- body
    component Quote: Rectangle {
        property alias text: quoteText.text
        property real maxHeight: Style.px(220)
        Layout.fillWidth: true
        implicitHeight: Math.min(quoteText.implicitHeight, maxHeight) + Style.px(20)
        radius: Style.radius
        color: Style.alpha(Style.bg, 0.6)
        border.width: 1
        border.color: Style.line
        Flickable {
            id: quoteFlick
            anchors { fill: parent; margins: Style.px(10) }
            contentHeight: quoteText.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: quoteFlick.contentHeight > quoteFlick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
            TextEdit {
                id: quoteText
                width: quoteFlick.width - Style.px(8)
                color: Style.text
                font.family: dlg.mono
                font.pixelSize: Style.fontSmall
                wrapMode: Text.WrapAnywhere
                readOnly: true
                selectByMouse: true
                selectionColor: Style.alpha(Style.accent, 0.4)
                textFormat: TextEdit.PlainText
            }
        }
    }

    component Disclosure: AbstractButton {
        id: disc
        property bool expanded: false
        Layout.fillWidth: true
        implicitHeight: Style.px(32)
        hoverEnabled: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        background: Item {}
        contentItem: RowLayout {
            spacing: Style.px(6)
            Icon { name: "chevron-right"; size: Style.px(16); color: Style.textMuted; rotation: disc.expanded ? 90 : 0
                   Behavior on rotation { NumberAnimation { duration: Style.dur(120) } } }
            Txt { text: disc.text; muted: !disc.hovered; font.weight: Font.Medium; Layout.fillWidth: true }
        }
    }

    Flickable {
        id: scroller
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(col.implicitHeight, Style.px(460))
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: scroller.contentHeight > scroller.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        ColumnLayout {
            id: col
            width: scroller.width - Style.px(10)
            spacing: Style.px(12)

            // Causes the log states, one card each
            Repeater {
                model: dlg.findings
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property var f: modelData
                    readonly property var st: dlg.fixState[dlg.fixKey(f)] || ({})
                    readonly property string label: dlg.fixLabel(f)
                    Layout.fillWidth: true
                    implicitHeight: cardCol.implicitHeight + Style.px(28)
                    radius: Style.radius
                    color: Style.alpha(Style.surfaceHi, 0.5)
                    border.width: 1
                    border.color: Style.line

                    ColumnLayout {
                        id: cardCol
                        anchors { fill: parent; margins: Style.px(14) }
                        spacing: Style.px(8)
                        Txt {
                            Layout.fillWidth: true
                            text: dlg.headline(card.f)
                            font.pixelSize: Style.fontLead
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                        Txt { Layout.fillWidth: true; text: dlg.detail(card.f); muted: true; wrapMode: Text.WordWrap }
                        Txt { text: qsTr("From the log:"); muted: true; font.pixelSize: Style.fontSmall; Layout.topMargin: Style.px(2) }
                        Quote { text: card.f.evidence.join("\n"); maxHeight: Style.px(120) }
                        Txt {
                            visible: text.length > 0
                            Layout.fillWidth: true
                            text: dlg.fixNote(card.f)
                            muted: true
                            font.pixelSize: Style.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            visible: card.label.length > 0 || !!card.st.state
                            Layout.fillWidth: true
                            spacing: Style.px(10)
                            PrimaryButton {
                                visible: card.label.length > 0 && card.st.state !== "done"
                                implicitHeight: Style.px(40)
                                text: card.st.state === "working" ? qsTr("Working…") : card.label
                                enabled: card.st.state !== "working"
                                onClicked: dlg.runFix(card.f)
                            }
                            Icon { visible: card.st.state === "done"; name: "check"; size: Style.px(18); color: Style.success }
                            Txt {
                                visible: !!card.st.message
                                Layout.fillWidth: true
                                text: card.st.message || ""
                                color: card.st.state === "failed" ? Style.danger : Style.text
                                wrapMode: Text.WordWrap
                            }
                            QuietButton {
                                // the mod isn't on Modrinth under this exact id: let the user search
                                visible: card.f.fix === "install" && card.st.state === "failed"
                                text: qsTr("Search in mods")
                                onClicked: { Tools.requestFind(dlg.instanceId, card.f.dependency); dlg.close(); Router.openInstance(dlg.instanceId, "mods") }
                            }
                        }
                    }
                }
            }

            // Where the error starts (always for an unknown cause; on request otherwise)
            Disclosure {
                visible: dlg.known && !!dlg.report.excerpt
                text: qsTr("Log excerpt")
                expanded: dlg.showExcerpt
                onClicked: dlg.showExcerpt = !dlg.showExcerpt
            }
            Quote {
                visible: !!dlg.report.excerpt && (!dlg.known || dlg.showExcerpt)
                text: dlg.report.excerpt || ""
                maxHeight: Style.px(dlg.known ? 200 : 300)
            }

            // Mods in the stack trace: a hint for whoever reads it, never a verdict
            Disclosure {
                visible: !dlg.known && (dlg.report.mentioned || []).length > 0
                text: qsTr("Mods mentioned in the error (%1)").arg((dlg.report.mentioned || []).length)
                expanded: dlg.showMentioned
                onClicked: dlg.showMentioned = !dlg.showMentioned
            }
            ColumnLayout {
                visible: dlg.showMentioned && !dlg.known
                Layout.fillWidth: true
                Layout.leftMargin: Style.px(22)
                spacing: Style.px(6)
                Txt {
                    Layout.fillWidth: true
                    text: qsTr("They appear in the stack trace. That does not mean they caused the crash: in big modpacks the mods named in an error are often not the ones at fault.")
                    muted: true
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.WordWrap
                }
                Txt {
                    Layout.fillWidth: true
                    text: (dlg.report.mentioned || []).join(", ")
                    font.family: dlg.mono
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }

    // Sharing uploads the log: say where and ask first
    RowLayout {
        visible: dlg.confirmShare
        Layout.fillWidth: true
        spacing: Style.px(10)
        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSmall
            color: Launcher.logShare.state === "failed" ? Style.danger : Launcher.logShare.state === "done" ? Style.success : Style.text
            text: Launcher.logShare.state === "done" ? qsTr("Link copied: %1").arg(Launcher.logShare.url)
                : Launcher.logShare.state === "failed" ? Launcher.logShare.error
                : qsTr("The log will be uploaded to %1 and the link copied. Folder paths with your user name and login tokens are hidden.").arg(Launcher.pasteHost())
        }
        QuietButton {
            visible: Launcher.logShare.state !== "done"
            text: Launcher.logShare.state === "uploading" ? qsTr("Uploading…") : qsTr("Upload")
            enabled: Launcher.logShare.state !== "uploading"
            onClicked: Launcher.shareLatestLog(dlg.instanceId)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(8)
        QuietButton { iconName: "copy"; text: qsTr("Copy"); onClicked: dlg.copyAll() }
        QuietButton { iconName: "terminal"; text: qsTr("Open full log"); onClicked: { dlg.close(); Router.openInstance(dlg.instanceId, "log") } }
        QuietButton { iconName: "link"; text: qsTr("Share log"); visible: !dlg.confirmShare; onClicked: dlg.confirmShare = true }
        Item { Layout.fillWidth: true }
        QuietButton { text: qsTr("Close"); onClicked: dlg.close() }
    }
}
