import QtQuick
import QtQuick.Shapes

// The Wild OS mark as the board draws it: two shapes in a 150 unit box.
Item {
    id: mark

    property real size: 38
    property color colour: "#ffffff"

    width: size
    height: size
    opacity: 0.95

    Shape {
        x: -18 * mark.size / 150
        y: 10.5 * mark.size / 150
        scale: mark.size / 150
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillColor: mark.colour
            PathSvg { path: "M37 17 L50.34 40 L64.75 64.84 L79.16 40 L92.5 17 L105.84 40 L134.26 89 L120.76 112.28 L92.5 63.55 L64.91 111.12 L23.66 40Z" }
        }

        ShapePath {
            strokeColor: "transparent"
            fillColor: mark.colour
            PathSvg { path: "M148.5 17 L162 40 L148.5 63 L135 40Z" }
        }
    }
}
