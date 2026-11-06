import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Change the Minecraft version and mod loader of an existing instance.
Item {
    id: page
    property var params: ({})
    readonly property string instanceId: params.instanceId || ""
    readonly property var info: Launcher.versionInfo(instanceId)
    property string error: ""

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
                    text: qsTr("Version and mod loader")
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.px(28)
                    font.weight: Font.DemiBold
                }
                Txt {
                    muted: true
                    text: page.info.loader ? qsTr("Now: Minecraft %1 with %2 %3").arg(page.info.mc).arg(picker.loaderLabel(page.info.loader)).arg(page.info.loaderVersion)
                                           : qsTr("Now: Minecraft %1 without a mod loader").arg(page.info.mc)
                }
            }
        }

        CreateVanilla {
            id: picker
            Layout.fillWidth: true
            Layout.fillHeight: true
            mcVersion: page.info.mc || ""
            loader: page.info.loader || ""
            loaderVersion: page.info.loaderVersion || ""
            function loaderLabel(key) { const o = loaderOptions.find(x => x.value === key); return o ? o.label : key }
        }

        // Footer
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
                Txt {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: page.error ? Style.danger : Style.textMuted
                    text: page.error || picker.blocker
                          || qsTr("Mods made for the old version may stop working. Worlds opened in a newer version can't go back to an older one — make a copy first if unsure.")
                }
                QuietButton { text: qsTr("Cancel"); onClicked: Router.back() }
                PrimaryButton {
                    text: qsTr("Apply")
                    iconName: "check"
                    enabled: picker.ready
                    onClicked: {
                        page.error = Launcher.changeVersion(page.instanceId, picker.mcVersion, picker.loader, picker.loaderVersion)
                        if (!page.error) { Instances.refreshMeta(); Router.toast(qsTr("Version changed")); Router.back() }
                    }
                }
            }
        }
    }
}
