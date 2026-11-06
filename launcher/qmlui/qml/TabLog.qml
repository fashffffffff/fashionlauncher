import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import FFPrism

// The game log: live output while the game runs, the last log file otherwise.
Item {
    id: tab
    property string instanceId
    property var instance
    property bool follow: true
    property bool restartPending: false
    readonly property var launch: Launcher.launches[instanceId]
    readonly property bool running: !!(instance && instance.running) || !!launch

    GameLog {
        id: log
        instanceId: tab.instanceId
        onSourceChanged: if (source === "none") loadLastLogFile()
        Component.onCompleted: if (source === "none") loadLastLogFile()
    }

    // Restart: wait until the old game is gone, then start again.
    Connections {
        target: Launcher
        function onLaunchesChanged() {
            if (tab.restartPending && !Launcher.launches[tab.instanceId]) {
                tab.restartPending = false
                Launcher.launch(tab.instanceId)
            }
        }
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        title: qsTr("Save game log")
        nameFilters: [qsTr("Log files (*.log *.txt)")]
        onAccepted: {
            const err = log.saveTo(selectedFile.toString())
            Router.toast(err || qsTr("Log saved."), !!err)
        }
    }

    component Chip: AbstractButton {
        id: chip
        property color dot: Style.text
        property int count: -1
        checkable: true
        implicitHeight: Style.px(32)
        implicitWidth: chipRow.implicitWidth + Style.px(22)
        hoverEnabled: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: text
        background: Rectangle {
            radius: height / 2
            color: chip.checked ? Style.alpha(chip.dot, 0.18) : chip.hovered ? Style.alpha(Style.text, 0.06) : "transparent"
            border.width: 1
            border.color: chip.checked ? Style.alpha(chip.dot, 0.6) : Style.line
        }
        contentItem: Item {
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: Style.px(6)
                Rectangle { width: Style.px(8); height: width; radius: width / 2; color: chip.dot; anchors.verticalCenter: parent.verticalCenter; opacity: chip.checked ? 1 : 0.4 }
                Txt { text: chip.text; muted: !chip.checked; font.pixelSize: Style.fontSmall; anchors.verticalCenter: parent.verticalCenter }
                Txt {
                    visible: chip.count > 0
                    text: chip.count
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    color: chip.dot
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    readonly property color warnColor: "#E8B341"
    function lineColor(level) {
        return level === "error" ? Style.danger : level === "warn" ? warnColor : level === "launcher" ? Style.accent
             : level === "debug" ? Style.textMuted : Style.text
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.px(10)

        // Toolbar: search, level filters, actions
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Field {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: Style.px(320)
                iconName: "search"
                placeholderText: qsTr("Search the log")
                onTextChanged: log.query = text
                Keys.onEscapePressed: text = ""
            }
            Chip { text: qsTr("Launcher"); dot: Style.accent; checked: log.showLauncher; onToggled: log.showLauncher = checked }
            Chip { text: qsTr("Info"); dot: Style.text; checked: log.showInfo; onToggled: log.showInfo = checked }
            Chip { text: qsTr("Warnings"); dot: tab.warnColor; count: log.warningCount; checked: log.showWarnings; onToggled: log.showWarnings = checked }
            Chip { text: qsTr("Errors"); dot: Style.danger; count: log.errorCount; checked: log.showErrors; onToggled: log.showErrors = checked }
            Item { Layout.fillWidth: true }
            QuietButton { iconName: "copy"; tip: qsTr("Copy the log"); onClicked: { log.copy(search.text.length > 0); Router.toast(qsTr("Log copied."), false) } }
            QuietButton {
                iconName: "export"; tip: qsTr("Save to a file")
                onClicked: { saveDialog.selectedFile = "file://" + log.suggestedFileName(); saveDialog.open() }
                enabled: log.totalLines > 0
            }
            QuietButton { iconName: "link"; tip: qsTr("Upload and copy link"); onClicked: shareConfirm.open() }
            QuietButton { iconName: "trash"; tip: qsTr("Clear"); enabled: log.totalLines > 0; onClicked: log.clear() }
        }

        // The log itself
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radius
            color: Style.alpha(Style.bg, 0.55)
            border.width: 1
            border.color: Style.line
            clip: true

            ListView {
                id: lines
                anchors { fill: parent; margins: Style.px(10) }
                model: log
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 2000
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                // Follow new lines while at the bottom; scrolling up pauses it.
                onCountChanged: if (tab.follow) Qt.callLater(positionViewAtEnd)
                onMovementEnded: tab.follow = atYEnd
                onFlickEnded: tab.follow = atYEnd
                delegate: TextEdit {
                    width: lines.width - Style.px(12)
                    text: model.line
                    color: tab.lineColor(model.level)
                    font.family: Qt.platform.os === "windows" ? "Consolas" : Qt.platform.os === "osx" ? "Menlo" : "monospace"
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.WrapAnywhere
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.alpha(Style.accent, 0.4)
                    textFormat: TextEdit.PlainText
                }
            }

            Column {
                anchors.centerIn: parent
                visible: lines.count === 0
                spacing: Style.px(8)
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "terminal"; size: Style.px(36); color: Style.textMuted }
                Txt {
                    anchors.horizontalCenter: parent.horizontalCenter
                    muted: true
                    text: log.totalLines > 0 ? qsTr("No lines match the filter.")
                        : tab.running ? qsTr("Waiting for the game to say something…")
                        : qsTr("No game log yet. Play once and the output shows up here.")
                }
            }

            QuietButton {
                visible: !tab.follow && lines.count > 0
                anchors { right: parent.right; bottom: parent.bottom; margins: Style.px(14) }
                iconName: "arrow-left"
                text: qsTr("Jump to the end")
                onClicked: { tab.follow = true; lines.positionViewAtEnd() }
            }
        }

        // Status and game controls
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(8)
            Txt {
                Layout.fillWidth: true
                muted: true
                font.pixelSize: Style.fontSmall
                text: (log.source === "live" ? (tab.running ? qsTr("Live output") : qsTr("Output of the last launch"))
                       : log.source === "file" ? qsTr("From %1").arg(log.fileName) : "")
                      + (log.totalLines ? "  ·  " + qsTr("%n line(s)", "", log.totalLines) : "")
            }
            SettingsToggle { label: qsTr("Auto-scroll"); checked: tab.follow; onToggled: (on) => { tab.follow = on; if (on) lines.positionViewAtEnd() } }
            QuietButton {
                visible: tab.running
                iconName: "stop"; text: qsTr("Kill"); danger: true
                onClicked: Launcher.kill(tab.instanceId)
            }
            QuietButton {
                iconName: "refresh"
                text: tab.running ? qsTr("Restart") : qsTr("Play")
                enabled: !tab.restartPending
                onClicked: {
                    if (tab.running) { tab.restartPending = true; Launcher.kill(tab.instanceId) }
                    else Launcher.launch(tab.instanceId)
                }
            }
        }
    }

    // Upload: outward-facing, so say where it goes and ask first.
    ModalCard {
        id: shareConfirm
        readonly property string file: { opened; return Launcher.latestLogFile(tab.instanceId) }
        onAboutToShow: Launcher.resetLogShare()
        title: qsTr("Share the game log?")
        subtitle: file
            ? qsTr("“%1” will be uploaded to %2 and the link copied, so you can send it to someone who helps you. Folder paths with your user name and login tokens are hidden.")
                  .arg(file.split("/").pop()).arg(Launcher.pasteHost())
            : qsTr("There is no game log yet. Play once first.")
        Txt {
            visible: Launcher.logShare.state === "done" || Launcher.logShare.state === "failed"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            color: Launcher.logShare.state === "failed" ? Style.danger : Style.success
            text: Launcher.logShare.state === "done" ? qsTr("Link copied: %1").arg(Launcher.logShare.url) : (Launcher.logShare.error || "")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Style.px(8)
            QuietButton { text: Launcher.logShare.state === "done" ? qsTr("Close") : qsTr("Cancel"); onClicked: shareConfirm.close() }
            QuietButton {
                visible: Launcher.logShare.state === "done"
                iconName: "link"; text: qsTr("Open link")
                onClicked: Launcher.openUrl(Launcher.logShare.url)
            }
            PrimaryButton {
                visible: !!shareConfirm.file && Launcher.logShare.state !== "done"
                implicitHeight: Style.px(40)
                text: Launcher.logShare.state === "uploading" ? qsTr("Uploading…") : qsTr("Upload and copy link")
                enabled: Launcher.logShare.state !== "uploading"
                onClicked: Launcher.shareLatestLog(tab.instanceId)
            }
        }
    }
}
