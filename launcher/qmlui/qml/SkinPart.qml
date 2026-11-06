import QtQuick
import QtQuick3D
import FFPrism.Skins

// One body part of the 3D player: the base box and, slightly larger, its outer layer.
Node {
    id: part
    property vector3d boxSize: Qt.vector3d(8, 8, 8)
    property point baseUv
    property point outerUv
    property real inflate: 0.25
    property bool hasOuter: true
    property size textureSize: Qt.size(64, 64)
    property Material baseMaterial
    property Material outerMaterial

    Model {
        geometry: SkinBox { boxSize: part.boxSize; uv: part.baseUv; textureSize: part.textureSize }
        materials: [part.baseMaterial]
    }
    Model {
        visible: part.hasOuter
        geometry: SkinBox { boxSize: part.boxSize; uv: part.outerUv; textureSize: part.textureSize; inflate: part.inflate }
        materials: [part.outerMaterial]
    }
}
