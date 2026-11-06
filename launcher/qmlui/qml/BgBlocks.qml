import QtQuick

// Generated block horizon: a skyline of square blocks, seeded by the instance id.
Canvas {
    id: blocks
    property string seed: "world"
    renderStrategy: Canvas.Cooperative
    property color topColor: Style.mix(Style.surfaceHi, Style.accent, 0.55)
    property color midColor: Style.surfaceHi
    property color deepColor: Style.surface
    onTopColorChanged: requestPaint()
    onDeepColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onSeedChanged: requestPaint()

    function hash(s) {
        let h = 2166136261
        for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) }
        return h >>> 0
    }
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const size = Math.max(18, Math.round(width / 56))
        const gap = Math.max(1, Math.round(size / 14))
        const cols = Math.ceil(width / size) + 1
        let rnd = hash(seed)
        const next = () => { rnd = (Math.imul(rnd, 1664525) + 1013904223) >>> 0; return rnd / 4294967296 }
        const baseRows = Math.max(3, Math.round(height * 0.2 / size))
        let h = baseRows
        for (let x = 0; x < cols; x++) {
            // random walk with plateaus, like terrain
            const r = next()
            if (r < 0.18) h = Math.max(2, h - 1)
            else if (r > 0.82) h = Math.min(Math.round(baseRows * 1.8), h + 1)
            for (let y = 0; y < h; y++) {
                const depth = h - 1 - y
                const px = x * size, py = height - (y + 1) * size
                if (depth === 0) {
                    ctx.fillStyle = topColor
                    ctx.globalAlpha = 0.55 + next() * 0.2
                } else {
                    ctx.fillStyle = depth < 3 ? midColor : deepColor
                    // fade into the ground the deeper it goes
                    ctx.globalAlpha = Math.max(0.12, 0.5 - depth * 0.07) * (0.8 + next() * 0.4)
                }
                ctx.fillRect(px, py, size - gap, size - gap)
            }
        }
    }
}
