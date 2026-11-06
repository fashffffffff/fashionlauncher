import QtQuick
import QtQuick.Shapes

// Stroke icons drawn as vector paths on a 24-unit grid, so they recolor with the theme.
Item {
    id: root
    property string name
    property color color: Style.text
    property real size: 20
    property real weight: 1.8
    implicitWidth: size
    implicitHeight: size

    readonly property var paths: ({
        "play": "M8 5.2v13.6a.6.6 0 0 0 .9.5l10.6-6.8a.6.6 0 0 0 0-1L8.9 4.7a.6.6 0 0 0-.9.5z",
        "stop": "M7 7h10v10H7z",
        "folder": "M3.5 7A1.5 1.5 0 0 1 5 5.5h4l2 2.5h8a1.5 1.5 0 0 1 1.5 1.5V18a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 18z",
        "cube": "M12 3.2l7.8 4.4v8.8L12 20.8l-7.8-4.4V7.6zM12 12l7.8-4.4M12 12v8.8M12 12L4.2 7.6",
        "sliders": "M4 7h9M17 7h3M15 5v4M4 17h3M11 17h9M9 15v4",
        "more": "M5.5 12h.01M12 12h.01M18.5 12h.01",
        "plus": "M12 5v14M5 12h14",
        "search": "M10.5 4.5a6 6 0 1 0 0 12a6 6 0 1 0 0-12zM15 15l5 5",
        "grid": "M4.5 4.5h6v6h-6zM13.5 4.5h6v6h-6zM4.5 13.5h6v6h-6zM13.5 13.5h6v6h-6z",
        "close": "M6.5 6.5l11 11M17.5 6.5l-11 11",
        "terminal": "M5 7l4.5 4.5L5 16M12 17h7",
        "check": "M5 12.5l4.5 4.5L19 7.5",
        "image": "M4 5.5h16v13H4zM4 15l4.5-4.5 4 4 2.5-2.5 5 5M15 9.2h.01",
        "copy": "M8.5 8.5h11v11h-11zM15.5 8.5V4.5h-11v11h4",
        "trash": "M5 7h14M10 7V4.5h4V7M7 7l1 12.5h8L17 7",
        "export": "M12 15V4M8 8l4-4 4 4M5 13v6.5h14V13",
        "pencil": "M15.5 5.5l3 3L9 18l-4 1 1-4z",
        "user": "M12 4a4 4 0 1 0 0 8a4 4 0 1 0 0-8zM4.5 20c1.2-3.5 4-5 7.5-5s6.3 1.5 7.5 5",
        "link": "M10 14l4-4M8.5 11.5L6.8 13.2a3 3 0 0 0 4.2 4.2l1.7-1.7M15.5 12.5l1.7-1.7a3 3 0 0 0-4.2-4.2l-1.7 1.7",
        "palette": "M12 3.5a8.5 8.5 0 1 0 0 17c1 0 1.5-.7 1.5-1.4 0-1-.9-1.3-.9-2.2 0-.8.6-1.4 1.5-1.4H16a4.5 4.5 0 0 0 4.5-4.5c0-4.2-3.8-7.5-8.5-7.5zM7.5 11.5h.01M10 7.5h.01M14.5 7.5h.01",
        "arrow-left": "M19 12H5M11 6l-6 6 6 6",
        "home": "M4 11l8-6.5 8 6.5M6 9.5V19.5h4.5v-5h3v5H18V9.5",
        "gear": "M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6zM12 3.5v2.2M12 18.3v2.2M20.5 12h-2.2M5.7 12H3.5M18 6l-1.6 1.6M7.6 16.4L6 18M18 18l-1.6-1.6M7.6 7.6L6 6",
        "compass": "M12 3.5a8.5 8.5 0 1 0 0 17a8.5 8.5 0 1 0 0-17zM15.5 8.5l-2 5-5 2 2-5z",
        "globe": "M12 3.5a8.5 8.5 0 1 0 0 17a8.5 8.5 0 1 0 0-17zM3.5 12h17M12 3.5c2.5 2.5 3.5 5.5 3.5 8.5s-1 6-3.5 8.5c-2.5-2.5-3.5-5.5-3.5-8.5s1-6 3.5-8.5",
        "world": "M4 9l8-4.5L20 9v6l-8 4.5L4 15zM4 9l8 4.5L20 9M12 13.5v6",
        "chevron-right": "M10 6l6 6-6 6",
        "refresh": "M19.5 12a7.5 7.5 0 1 1-2.2-5.3M19.5 4.5v4h-4"
    })

    Shape {
        width: 24
        height: 24
        scale: root.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.name === "play" ? "transparent" : root.color
            fillColor: root.name === "play" ? root.color : "transparent"
            strokeWidth: root.name === "more" ? 3 : root.weight
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.paths[root.name] || "" }
        }
    }
}
