import QtQuick
import QtQuick3D
import FFPrism.Skins

// The player in 3D: drag to turn it, double-click to face front again. Breathes and sways a little while idle.
Item {
    id: viewer
    property string skinPath
    property bool slim: false
    property string capePath
    property real yaw: -28
    property real pitch: 6
    property bool animate: Style.motion

    readonly property real armW: slim ? 3 : 4
    readonly property bool hasCape: capePath.length > 0 && capeTex.loaded
    // Picking a cape turns the player around so you actually see it
    // (not on the first load of the page, only when the user picks one)
    property bool settled: false
    Timer { interval: 1200; running: true; onTriggered: viewer.settled = true }
    onSkinPathChanged: { settled = false; settleAgain.restart() }
    Timer { id: settleAgain; interval: 600; onTriggered: viewer.settled = true }
    onCapePathChanged: if (settled && capePath.length > 0 && Style.motion) showBack.start()
    NumberAnimation { id: showBack; target: viewer; property: "yaw"; to: 155; duration: Style.dur(600); easing.type: Easing.InOutCubic }

    // Idle motion: one slow cycle drives breathing, arm sway and a lazy look around
    property real t: 0
    NumberAnimation on t {
        running: viewer.animate && viewer.visible
        from: 0; to: Math.PI * 2; duration: 6000; loops: Animation.Infinite
    }
    readonly property real sway: Math.sin(t) * 3
    readonly property real look: Math.sin(t * 0.5) * 8

    View3D {
        id: view
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        PerspectiveCamera {
            position: Qt.vector3d(0, 17, 78)
            fieldOfView: 32
            clipNear: 1
            clipFar: 400
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-25, -30, 0)
            brightness: 0.85
            ambientColor: Qt.rgba(0.55, 0.55, 0.58, 1)
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-10, 150, 0)
            brightness: 0.35
        }

        Texture {
            id: skinTexture
            textureData: SkinTexture { id: skinTex; source: viewer.skinPath }
            minFilter: Texture.Nearest
            magFilter: Texture.Nearest
            mipFilter: Texture.None
        }
        Texture {
            id: capeTexture
            textureData: SkinTexture { id: capeTex; source: viewer.capePath; cape: true }
            minFilter: Texture.Nearest
            magFilter: Texture.Nearest
            mipFilter: Texture.None
        }
        // The base layer is opaque, like in the game; the outer layer keeps only its painted pixels.
        PrincipledMaterial { id: baseMat; baseColorMap: skinTexture; roughness: 1; metalness: 0; specularAmount: 0 }
        PrincipledMaterial {
            id: outerMat
            baseColorMap: skinTexture; roughness: 1; metalness: 0; specularAmount: 0
            alphaMode: PrincipledMaterial.Mask; alphaCutoff: 0.5
            cullMode: Material.NoCulling
        }
        PrincipledMaterial { id: capeMat; baseColorMap: capeTexture; roughness: 1; metalness: 0; specularAmount: 0; cullMode: Material.NoCulling }

        Node {
            id: player
            visible: skinTex.loaded
            eulerRotation: Qt.vector3d(viewer.pitch, viewer.yaw, 0)
            // Turn around the middle of the body (feet are at 0, head top at 32), not around the feet;
            // a pivot is placed at the node's position, so lift it by the same amount. Plus breathing.
            pivot: Qt.vector3d(0, 16, 0)
            y: 16 + Math.sin(viewer.t * 2) * 0.15

            // Head turns at the neck
            Node {
                y: 24
                eulerRotation: Qt.vector3d(Math.sin(viewer.t) * 2, viewer.look, 0)
                SkinPart {
                    y: 4
                    boxSize: Qt.vector3d(8, 8, 8)
                    baseUv: Qt.point(0, 0); outerUv: Qt.point(32, 0); inflate: 0.5
                    baseMaterial: baseMat; outerMaterial: outerMat
                }
            }
            SkinPart {
                y: 18
                boxSize: Qt.vector3d(8, 12, 4)
                baseUv: Qt.point(16, 16); outerUv: Qt.point(16, 32)
                baseMaterial: baseMat; outerMaterial: outerMat
            }
            // Arms swing from the shoulders (the player's right arm is on screen-left)
            Node {
                x: -(4 + viewer.armW / 2); y: 22
                eulerRotation: Qt.vector3d(viewer.sway, 0, -2 - Math.abs(viewer.sway) * 0.3)
                SkinPart {
                    y: -4
                    boxSize: Qt.vector3d(viewer.armW, 12, 4)
                    baseUv: Qt.point(40, 16); outerUv: Qt.point(40, 32)
                    baseMaterial: baseMat; outerMaterial: outerMat
                }
            }
            Node {
                x: 4 + viewer.armW / 2; y: 22
                eulerRotation: Qt.vector3d(-viewer.sway, 0, 2 + Math.abs(viewer.sway) * 0.3)
                SkinPart {
                    y: -4
                    boxSize: Qt.vector3d(viewer.armW, 12, 4)
                    baseUv: Qt.point(32, 48); outerUv: Qt.point(48, 48)
                    baseMaterial: baseMat; outerMaterial: outerMat
                }
            }
            SkinPart {
                x: -2; y: 6
                boxSize: Qt.vector3d(4, 12, 4)
                baseUv: Qt.point(0, 16); outerUv: Qt.point(0, 32)
                baseMaterial: baseMat; outerMaterial: outerMat
            }
            SkinPart {
                x: 2; y: 6
                boxSize: Qt.vector3d(4, 12, 4)
                baseUv: Qt.point(16, 48); outerUv: Qt.point(0, 48)
                baseMaterial: baseMat; outerMaterial: outerMat
            }
            // Cape hangs from the shoulders and leans back a little
            Node {
                visible: viewer.hasCape
                y: 24; z: -2.6
                eulerRotation: Qt.vector3d(8 + Math.sin(viewer.t) * 2, 0, 0)
                Model {
                    y: -8
                    eulerRotation.y: 180
                    geometry: SkinBox {
                        boxSize: Qt.vector3d(10, 16, 1)
                        uv: Qt.point(0, 0)
                        // HD capes keep the 64×32 proportions; ancient 22×17 ones have their own
                        textureSize: capeTex.imageSize.width > 0 && capeTex.imageSize.width % 64 !== 0 ? Qt.size(22, 17) : Qt.size(64, 32)
                    }
                    materials: [capeMat]
                }
            }
        }
    }

    // Soft floor shadow
    Rectangle {
        visible: skinTex.loaded
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.5 + Math.min(parent.height * 0.36, parent.width * 0.7)
        width: Math.min(parent.width, parent.height) * 0.32
        height: width * 0.16
        radius: height / 2
        color: Style.alpha("#000000", 0.25)
        z: -1
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real lastX
        property real lastY
        onPressed: (m) => { lastX = m.x; lastY = m.y }
        onPositionChanged: (m) => {
            viewer.yaw += (m.x - lastX) * 0.6
            viewer.pitch = Math.max(-35, Math.min(35, viewer.pitch + (m.y - lastY) * 0.3))
            lastX = m.x; lastY = m.y
        }
        onDoubleClicked: {
            // Take the short way back to the front
            let y = ((viewer.yaw % 360) + 360) % 360
            viewer.yaw = y > 180 ? y - 360 : y
            resetTurn.start()
        }
    }
    ParallelAnimation {
        id: resetTurn
        NumberAnimation { target: viewer; property: "yaw"; to: -28; duration: Style.dur(400); easing.type: Easing.OutCubic }
        NumberAnimation { target: viewer; property: "pitch"; to: 6; duration: Style.dur(400); easing.type: Easing.OutCubic }
    }
}
