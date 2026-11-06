import QtQuick
import QtMultimedia

// A muted, looping, cover-fitted video. No AudioOutput is created at all, so nothing is decoded
// for sound. The parent unloads this item whenever the video can't be seen, which frees the decoder.
Item {
    id: root
    property url source
    property bool paused: false
    // True once frames are coming, so the caller can fade its still picture away without a black flash.
    readonly property bool showing: player.playing || player.mediaStatus === MediaPlayer.BufferedMedia
    readonly property bool failed: player.error !== MediaPlayer.NoError

    VideoOutput {
        id: vo
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
    }
    MediaPlayer {
        id: player
        source: root.source
        loops: MediaPlayer.Infinite
        videoOutput: vo
        onErrorOccurred: (error, message) => console.warn("Background video:", message)
    }
    function sync() {
        if (root.paused) player.pause()
        else player.play()
    }
    onPausedChanged: sync()
    onSourceChanged: sync()
    Component.onCompleted: sync()
}
