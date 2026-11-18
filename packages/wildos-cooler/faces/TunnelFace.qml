import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real gpuTemp: number("gpuTemp", 61)
    readonly property real liquidTemp: number("liquidTemp", 34)

    Rectangle {
        width: 640
        height: 640
        color: "#030405"
    }

    Item {
        width: 640
        height: 640
        rotation: 360 * FaceMath.phase(face.time, 60, 0)

        // Ten hexagons, each a tenth of the way behind the last, flying out
        // from a dot to past the edge. Each is turned 9 degrees and moved 36
        // degrees round the colour wheel from the one before.
        Repeater {
            model: 10

            Shape {
                id: ring
                required property int index
                readonly property real flight: FaceMath.phase(face.time, 4, -0.4 * index)
                readonly property real hue: ((240 + 36 * index) % 360) / 360

                width: 640
                height: 640
                rotation: 9 * index
                scale: 0.1 + 2.4 * flight
                opacity: Math.min(1, flight / 0.12)
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Qt.hsla(ring.hue, 1, 0.62, 0.3)
                    strokeWidth: 13
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline {
                        path: [Qt.point(449.9, 395), Qt.point(320, 470), Qt.point(190.1, 395), Qt.point(190.1, 245),
                               Qt.point(320, 170), Qt.point(449.9, 245), Qt.point(449.9, 395)]
                    }
                }

                ShapePath {
                    strokeColor: Qt.hsla(ring.hue, 1, 0.74, 1)
                    strokeWidth: 4
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline {
                        path: [Qt.point(449.9, 395), Qt.point(320, 470), Qt.point(190.1, 395), Qt.point(190.1, 245),
                               Qt.point(320, 170), Qt.point(449.9, 245), Qt.point(449.9, 395)]
                    }
                }
            }
        }
    }

    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 320; centerY: 320; centerRadius: 215
                focalX: 320; focalY: 320
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.92) }
                GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.74) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0) }
            }
            PathAngleArc { centerX: 320; centerY: 320; radiusX: 215; radiusY: 215; startAngle: 0; sweepAngle: 360 }
        }
    }

    FaceText {
        id: lead
        baseY: 222
        size: 34
        spacing: 5
        font.weight: 600
        text: face.leadName
    }

    Reading {
        baseY: 352
        size: 146
        value: FaceMath.digits(face.cpuTemp)
        pair: face.pairValue
        label: lead
        pairScale: 0.54
        fitWidth: 330
    }

    FaceText {
        baseY: 402
        size: 34
        font.weight: 600
        opacity: 0.9
        text: face.has(face.gpuTemp) && !face.paired ? qsTr("%1 %2°").arg(face.otherName).arg(FaceMath.digits(face.gpuTemp)) : ""
    }

    FaceText {
        baseY: 442
        size: 34
        font.weight: 600
        opacity: 0.9
        text: face.has(face.liquidTemp) ? qsTr("LIQ %1°").arg(FaceMath.digits(face.liquidTemp)) : ""
    }

    Mark {
        visible: face.showMark
        x: 308
        y: 462
        size: 24
    }
}
