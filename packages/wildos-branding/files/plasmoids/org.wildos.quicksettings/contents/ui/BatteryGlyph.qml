import QtQuick
import QtQuick.Shapes
import WildOS.UI as UI

// The design's battery: a 21 by 12 body with a nub, filled to the charge
// level. Being plugged in swaps the fill for a bolt, and a nearly empty battery that
// is not plugged in fills in the warning colour.
Item {
    id: glyph

    property int percent: 100
    property bool charging: false
    property bool low: false
    property color color: UI.Style.iconInk

    // The fill spans x 5 to 20 of the 28 by 24 artwork.
    readonly property real fillWidth: Math.max(1.4, 15 * Math.min(100, Math.max(0, percent)) / 100)

    implicitWidth: 22
    implicitHeight: 18

    function roundedRect(x: real, y: real, w: real, h: real, r: real): string {
        const rr = Math.min(r, w / 2, h / 2);
        return `M${x + rr} ${y}h${w - 2 * rr}a${rr} ${rr} 0 0 1 ${rr} ${rr}v${h - 2 * rr}`
            + `a${rr} ${rr} 0 0 1 -${rr} ${rr}h-${w - 2 * rr}a${rr} ${rr} 0 0 1 -${rr} -${rr}`
            + `v-${h - 2 * rr}a${rr} ${rr} 0 0 1 ${rr} -${rr}z`;
    }

    Shape {
        width: 28
        height: 24
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: glyph.width / 28; yScale: glyph.height / 24 }

        ShapePath {
            strokeColor: glyph.color
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg { path: glyph.roundedRect(2, 6, 21, 12, 3) + "M26 10.5v3" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: glyph.charging ? "transparent" : glyph.low ? UI.Style.warning : glyph.color
            PathSvg { path: glyph.roundedRect(5, 9, glyph.fillWidth, 6, 1.2) }
        }
        ShapePath {
            strokeColor: glyph.charging ? glyph.color : "transparent"
            strokeWidth: 1.8
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M14 8.2l-3.2 4.2h3.8l-3.2 4.2" }
        }
    }
}
