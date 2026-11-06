pragma Singleton
import QtQuick
import QtMultimedia

// UI sounds. Call Sounds.play("click" | "toggle" | "success" | "error" | "launch").
// Respects Gallery.soundsOn / Gallery.soundVolume; each sound restarts instead of overlapping itself.
QtObject {
    id: sounds
    readonly property bool on: Gallery.soundsOn
    // perceived loudness is roughly logarithmic: square the slider value so the low end stays usable
    readonly property real level: Math.pow(Math.max(0, Math.min(1, Gallery.soundVolume)), 2)

    function _make(file) {
        return effectComponent.createObject(sounds, { file: file })
    }
    property Component effectComponent: Component {
        SoundEffect {
            property string file
            property real lastPlayed: 0
            // no source while sounds are off, so the audio device is never opened
            source: sounds.on ? "qrc:/qmlui/sounds/" + file + ".wav" : ""
            volume: sounds.level
        }
    }
    property var _effects: ({})
    readonly property var names: ["click", "toggle", "success", "error", "launch"]
    // create them up front so the first press is not delayed by decoding
    Component.onCompleted: { for (const n of names) _effects[n] = _make(n) }

    function play(name) {
        if (!on || level <= 0)
            return
        const fx = _effects[name]
        if (!fx)
            return
        // one press can reach two handlers (e.g. a button inside a nav item): play once
        const now = Date.now()
        if (now - fx.lastPlayed < 40)
            return
        fx.lastPlayed = now
        if (fx.playing)
            fx.stop()
        fx.play()
    }
}
