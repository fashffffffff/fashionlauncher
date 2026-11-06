import QtQuick
import QtQuick.Layouts

// How much memory the game may use. Global or per instance.
SettingsSection {
    id: ms
    group: "memory"
    title: qsTr("Memory")
    description: qsTr("More memory helps big modpacks. Leave some for your computer: about half of it is a safe maximum.")
    readonly property int ram: Settings.totalRamMiB
    readonly property int minMem: val("MinMemAlloc") || 512
    readonly property int maxMem: val("MaxMemAlloc") || 1024

    function gb(mib) { return qsTr("%1 GB").arg((mib / 1024).toLocaleString(Qt.locale(), "f", mib % 1024 === 0 ? 0 : 1)) }

    SettingsSlider {
        label: qsTr("Maximum memory")
        description: qsTr("The most the game can use.")
        from: 512; to: Math.max(ms.ram, 1024); stepSize: 256
        value: ms.maxMem
        unit: qsTr("MiB")
        hint: ms.gb(value)
        onCommitted: (v) => { ms.put("MaxMemAlloc", Math.round(v)); if (v < ms.minMem) ms.put("MinMemAlloc", Math.round(v)) }
    }
    SettingsSlider {
        label: qsTr("Memory at start")
        description: qsTr("Reserved when the game starts. Usually lower than the maximum.")
        from: 128; to: Math.max(ms.ram, 1024); stepSize: 128
        value: ms.minMem
        unit: qsTr("MiB")
        hint: ms.gb(value)
        onCommitted: (v) => { ms.put("MinMemAlloc", Math.round(v)); if (v > ms.maxMem) ms.put("MaxMemAlloc", Math.round(v)) }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Style.px(12)
        Txt {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            muted: true
            readonly property int rec: { Settings.revision; return Settings.recommendedMaxMemory(ms.instanceId) }
            text: qsTr("Recommended: %1 (based on this computer's memory%2).").arg(ms.gb(rec)).arg(ms.instanceId ? qsTr(" and the number of mods") : "")
        }
        QuietButton {
            iconName: "check"
            text: qsTr("Use recommended")
            onClicked: {
                const rec = Settings.recommendedMaxMemory(ms.instanceId)
                ms.put("MaxMemAlloc", rec)
                ms.put("MinMemAlloc", Math.min(1024, rec))
            }
        }
    }
    Txt {
        text: qsTr("This computer has %1 of memory.").arg(ms.gb(ms.ram))
        muted: true
        font.pixelSize: Style.fontSmall
    }
    SettingsNote {
        kind: ms.maxMem >= ms.ram ? "danger" : "warn"
        text: ms.maxMem >= ms.ram ? qsTr("The maximum is more than this computer has. The game may crash or freeze your computer.")
            : ms.maxMem * 10 > ms.ram * 9 ? qsTr("The maximum is close to all of this computer's memory. Other apps may slow down.")
            : ""
    }
}
