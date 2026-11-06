import QtQuick
import QtQuick3D

// Minecraft-style title-screen panorama: the camera sits inside a cube of six pictures and slowly turns.
// faces: [front, right, back, left, up, down] (panorama_0..5). Nothing is drawn while `running` is false:
// the camera stops, so the scene graph has nothing new to render.
Item {
    id: root
    property var faces: []
    property real speed: 1          // 1 = like the game's title screen (one turn in ~4 minutes)
    property bool running: true
    readonly property bool ready: faces.length === 6 && loaded === 6
    property int loaded: 0
    onFacesChanged: loaded = 0

    // Angle advanced by a 30 fps timer instead of a vsync animation: the turn is so slow nobody sees the
    // difference, and the whole window renders half as often.
    property real yaw: 0
    property real t: 0
    Timer {
        interval: 33
        repeat: true
        running: root.running && root.ready && root.speed > 0 && root.visible
        onTriggered: {
            root.yaw = (root.yaw - 1.5 * root.speed * interval / 1000) % 360
            root.t += interval / 1000
        }
    }

    View3D {
        id: view
        anchors.fill: parent
        opacity: root.ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Style.dur(600) } }
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "black"
            antialiasingMode: SceneEnvironment.NoAA
        }

        PerspectiveCamera {
            position: Qt.vector3d(0, 0, 0)
            fieldOfView: 70
            clipNear: 1
            clipFar: 200
            // A gentle nod up and down, like the title screen.
            eulerRotation: Qt.vector3d(4 + 3 * Math.sin(root.t * 0.05), root.yaw, 0)
        }

        // Each face is a 100×100 quad 50 units away; a hair larger than the cube so no seams show.
        component Face: Model {
            property int index
            source: "#Rectangle"
            scale: Qt.vector3d(1.003, 1.003, 1)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                cullMode: Material.NoCulling
                baseColorMap: Texture {
                    id: tex
                    source: root.faces.length === 6 ? root.faces[index] : ""
                    generateMipmaps: true
                    mipFilter: Texture.Linear
                    tilingModeHorizontal: Texture.ClampToEdge
                    tilingModeVertical: Texture.ClampToEdge
                }
            }
        }
        Face { index: 0; position: Qt.vector3d(0, 0, -50) }
        Face { index: 1; position: Qt.vector3d(50, 0, 0); eulerRotation.y: -90 }
        Face { index: 2; position: Qt.vector3d(0, 0, 50); eulerRotation.y: 180 }
        Face { index: 3; position: Qt.vector3d(-50, 0, 0); eulerRotation.y: 90 }
        Face { index: 4; position: Qt.vector3d(0, 50, 0); eulerRotation.x: 90 }
        Face { index: 5; position: Qt.vector3d(0, -50, 0); eulerRotation.x: -90 }
    }

    // Texture has no "loaded" signal; the faces are local files that load in a frame or two, so a plain
    // Image (tiny sourceSize, freed right after) tells us when the files are readable.
    Repeater {
        model: root.faces.length === 6 ? root.faces : []
        delegate: Image {
            visible: false
            source: modelData
            sourceSize: Qt.size(4, 4)
            asynchronous: true
            onStatusChanged: if (status === Image.Ready) root.loaded++
        }
    }
}
