import QtQuick

// A server's 64×64 icon, or a neutral globe when it has none.
Rectangle {
    property string source
    radius: Style.px(6)
    color: Style.alpha(Style.text, 0.06)
    clip: true
    Image { anchors.fill: parent; source: parent.source; visible: !!parent.source; smooth: false; fillMode: Image.PreserveAspectFit }
    Icon { anchors.centerIn: parent; visible: !parent.source; name: "globe"; color: Style.textMuted; size: parent.width * 0.5 }
}
