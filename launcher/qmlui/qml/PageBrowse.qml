import QtQuick
import QtQuick.Layouts
import "ResFmt.js" as RF

// Discover mods, resource packs and shaders on Modrinth and CurseForge and install them into an instance.
Item {
    id: page
    property var params: ({})
    property string instanceId: params.instanceId || ""
    property string kind: params.kind || "mods"
    /// Pushed from an instance's Mods/Resource packs/Shaders tab: show Back, keep that kind
    readonly property bool fromInstance: !!params.fromInstance

    // Instances to install into (rebuilt when the list changes)
    readonly property var instanceOptions: {
        Instances.count
        const out = []
        for (let i = 0; i < Instances.count; ++i) {
            const it = Instances.get(i)
            out.push({ value: it.instanceId, label: it.name, icon: it.icon,
                       detail: [it.gameVersion, it.loader].filter(s => s).join(" · ") })
        }
        return out
    }
    function pickDefault() {
        if (instanceId && instanceOptions.some(o => o.value === instanceId)) return
        const sel = Router.selectedId
        instanceId = instanceOptions.some(o => o.value === sel) ? sel : (instanceOptions.length ? instanceOptions[0].value : "")
    }
    Component.onCompleted: pickDefault()
    onInstanceOptionsChanged: pickDefault()
    Component.onDestruction: Instances.filter = ""

    ColumnLayout {
        // Opened from an instance it's a full-size catalog: tighter margins, one header row
        anchors { fill: parent; margins: page.fromInstance ? Style.px(20) : Style.px(32) }
        spacing: Style.px(14)

        // One header row: back, title, what to look for, where to install — the rest is results
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.px(12)
            QuietButton {
                visible: page.fromInstance
                iconName: "arrow-left"
                text: qsTr("Back")
                compact: true
                onClicked: Router.back()
            }
            Text {
                text: page.fromInstance ? RF.findLabel(page.kind) : qsTr("Browse mods")
                color: Style.text
                font.family: Style.display
                font.pixelSize: page.fromInstance ? Style.fontTitle : Style.px(32)
                font.weight: Font.DemiBold
            }
            ResSegmented {
                visible: page.instanceOptions.length > 0 && !page.fromInstance
                options: [{ value: "mods", label: qsTr("Mods") },
                          { value: "resourcepacks", label: qsTr("Resource packs") },
                          { value: "shaderpacks", label: qsTr("Shaders") }]
                value: page.kind
                onPicked: (v) => page.kind = v
            }
            Item { Layout.fillWidth: true }
            ResPicker {
                visible: page.instanceOptions.length > 0
                label: qsTr("Install into")
                options: page.instanceOptions
                value: page.instanceId
                implicitWidth: Style.px(300)
                onPicked: (v) => { page.instanceId = v; Router.selectedId = v }
            }
            QuietButton {
                visible: page.instanceId.length > 0 && !page.fromInstance
                text: qsTr("Open instance")
                iconName: "chevron-right"
                onClicked: Router.openInstance(page.instanceId, page.kind)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radius + Style.px(4)
            color: Style.panel(0)
            border.width: 1
            border.color: Style.panelBorder

            ResFindView {
                anchors { fill: parent; margins: Style.px(16) }
                visible: page.instanceOptions.length > 0
                instanceId: page.instanceId
                kind: page.kind
            }

            // No instance to install into
            ColumnLayout {
                anchors.centerIn: parent
                visible: page.instanceOptions.length === 0
                width: Math.min(parent.width - Style.px(40), Style.px(420))
                spacing: Style.px(12)
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Create an instance first")
                    color: Style.text
                    font.family: Style.display
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                }
                Txt {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    muted: true
                    text: qsTr("Mods are installed into an instance, so the right Minecraft version and loader are picked for you.")
                }
                PrimaryButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Create instance")
                    iconName: "plus"
                    onClicked: Router.open("create")
                }
            }
        }
    }
}
