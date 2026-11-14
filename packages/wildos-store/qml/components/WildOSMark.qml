import QtQuick
import QtQuick.Shapes
import WildOS.UI as UI

// The Wild OS mark, filled, as the Wild OS section's sidebar item and its
// summary bar draw it.
Item {
    id: mark

    property real size: 17
    property color color: UI.Style.inkAlpha(0.86, 0.86)

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 150
        height: 150
        preferredRendererType: Shape.CurveRenderer
        transform: [
            Translate { x: -18; y: 10.5 },
            Scale { xScale: mark.size / 150; yScale: mark.size / 150 }
        ]

        ShapePath {
            strokeColor: "transparent"
            fillColor: mark.color
            PathSvg {
                path: "M37 17 L50.34 40 L64.75 64.84 L79.16 40 L92.5 17 L105.84 40 L134.26 89 L120.76 112.28 L92.5 63.55 L64.91 111.12 L23.66 40Z"
            }
        }

        ShapePath {
            strokeColor: "transparent"
            fillColor: mark.color
            PathSvg {
                path: "M148.5 17 L162 40 L148.5 63 L135 40Z"
            }
        }
    }
}
