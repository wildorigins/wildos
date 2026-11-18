import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

// One temperature line across the face with the fade under it, for the two
// faces that show the last minute. The face decides the scale, so two lines
// can share it.
Item {
    id: graph

    property var values: []
    // Where the lowest grid line sits, the temperature it stands for, and
    // how many units one degree is worth.
    property real baseY: 468
    property real baseValue: 50
    property real perDegree: 5.2
    property real startX: 40
    property real endX: 600

    // The line runs through these three colours from left to right.
    property color lineStart: "#8fb4ff"
    property color lineMiddle: lineStart
    property color lineEnd: lineStart
    property color fillColour: lineStart
    property real fillOpacity: 0.55
    property color dotColour: "#ffffff"
    property color haloColour: "transparent"

    readonly property var points: {
        const v = values && values.length !== undefined ? values : []
        const out = []
        const n = v.length
        for (let i = 0; i < n; ++i) {
            const x = n > 1 ? startX + (endX - startX) * i / (n - 1) : endX
            out.push(Qt.point(x, baseY - (v[i] - baseValue) * perDegree))
        }
        // A single reading is still a line, flat across the face.
        if (n === 1)
            out.unshift(Qt.point(startX, out[0].y))
        return out
    }
    readonly property real highest: points.reduce((y, p) => Math.min(y, p.y), 640)
    readonly property point last: points.length ? points[points.length - 1] : Qt.point(endX, baseY)

    width: 640
    height: 640
    visible: points.length > 1

    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: graph.highest; x2: 0; y2: 640
                GradientStop { position: 0; color: FaceMath.alpha(graph.fillColour, graph.fillOpacity) }
                GradientStop { position: 1; color: FaceMath.alpha(graph.fillColour, 0) }
            }
            PathPolyline {
                path: graph.points.concat([Qt.point(graph.endX, 640), Qt.point(graph.startX, 640)])
            }
        }
    }

    // Qt Quick strokes take one colour, so the line is laid as short round
    // ended pieces, each in the gradient's colour where it sits.
    Repeater {
        model: Math.max(0, graph.points.length - 1)

        Streak {
            required property int index
            readonly property point from: graph.points[index]
            readonly property point to: graph.points[index + 1]

            x1: from.x
            y1: from.y
            x2: to.x
            y2: to.y
            thickness: 6
            color: FaceMath.gradientAt(graph.lineStart, graph.lineMiddle, graph.lineEnd, 0.55,
                                       FaceMath.clamp(((from.x + to.x) / 2 - 60) / 520, 0, 1))
        }
    }

    Rectangle {
        x: graph.last.x - 16
        y: graph.last.y - 16
        width: 32
        height: 32
        radius: 16
        color: graph.haloColour
        opacity: 0.3
    }

    Rectangle {
        x: graph.last.x - 8
        y: graph.last.y - 8
        width: 16
        height: 16
        radius: 8
        color: graph.dotColour
    }
}
