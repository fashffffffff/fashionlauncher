import QtQuick

// Flat front view of a skin for library cards: cheap, crisp, no 3D. Old 64×32 skins reuse the right limbs.
Item {
    id: front
    property string source
    property bool slim: false
    readonly property bool legacy: probe.status === Image.Ready && probe.implicitHeight === 32
    readonly property real u: Math.floor(Math.min(width / 16, height / 32))  // screen pixels per skin pixel
    readonly property real armW: slim ? 3 : 4
    implicitWidth: 64
    implicitHeight: 128

    Image { id: probe; source: !front.source ? "" : (front.source.startsWith("/") ? "file://" : "file:///") + front.source; visible: false }

    // Every rectangle: [x, y on the 16×32 grid, w, h, u, v in the texture, mirrored]
    readonly property var parts: {
        const a = armW
        const base = [
            [4, 0, 8, 8, 8, 8],                       // head
            [4, 8, 8, 12, 20, 20],                    // body
            [4 - a, 8, a, 12, 44, 20],                // right arm (screen left)
            [4, 20, 4, 12, 4, 20],                    // right leg
        ]
        if (legacy) base.push([12, 8, a, 12, 44, 20, true], [8, 20, 4, 12, 4, 20, true])
        else base.push([12, 8, a, 12, 36, 52], [8, 20, 4, 12, 20, 52])
        const outer = [[4, 0, 8, 8, 40, 8]]           // hat works on old skins too
        if (!legacy) outer.push([4, 8, 8, 12, 20, 36], [4 - a, 8, a, 12, 44, 36], [12, 8, a, 12, 52, 52],
                                [4, 20, 4, 12, 4, 36], [8, 20, 4, 12, 4, 52])
        return base.concat(outer)
    }

    Item {
        anchors.centerIn: parent
        width: front.u * 16
        height: front.u * 32
        Repeater {
            model: probe.status === Image.Ready ? front.parts : []
            delegate: Image {
                required property var modelData
                source: probe.source
                smooth: false
                sourceClipRect: Qt.rect(modelData[4], modelData[5], modelData[2], modelData[3])
                x: modelData[0] * front.u
                y: modelData[1] * front.u
                width: modelData[2] * front.u
                height: modelData[3] * front.u
                mirror: modelData[6] === true
            }
        }
    }
}
