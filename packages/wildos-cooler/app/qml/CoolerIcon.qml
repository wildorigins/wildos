import QtQuick
import QtQuick.Shapes
import WildOS.UI as UI

// One of the icons in CoolerIcons.qml, drawn as WildOS.UI's Icon draws its
// own: any size, stroke and colour.
Item {
    id: icon

    property string name
    property color color: UI.Style.inkMuted
    property real size: 17
    property real strokeWidth: 1.8

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 24
        height: 24
        preferredRendererType: Shape.CurveRenderer
        transform: Scale {
            xScale: icon.size / 24
            yScale: icon.size / 24
        }

        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: CoolerIcons.paths[icon.name] || ""
            }
        }
    }
}
