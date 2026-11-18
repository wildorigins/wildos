import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real gpuTemp: number("gpuTemp", 61)
    readonly property real cpuLoad: number("cpuLoad", 23)

    // A ring of equal arcs with a soft wider copy under each, the board's
    // glowing dashed circles. The arcs are drawn one by one so their ends are
    // square and the ring closes without a seam.
    component ArcRing: Item {
        id: ring

        property real radius: 100
        property real stroke: 4
        property real glow: 12
        property int count: 3
        // Length of one arc along the ring, in units.
        property real dash: 10
        property color colour: "#ffffff"
        property real strength: 1

        width: 640
        height: 640

        Repeater {
            model: ring.count

            Shape {
                required property int index

                width: 640
                height: 640
                rotation: index * 360 / ring.count
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Qt.rgba(ring.colour.r, ring.colour.g, ring.colour.b, 0.22)
                    strokeWidth: ring.glow
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap
                    PathAngleArc {
                        centerX: 320; centerY: 320; radiusX: ring.radius; radiusY: ring.radius
                        startAngle: 0
                        sweepAngle: ring.dash / ring.radius * 180 / Math.PI
                    }
                }

                ShapePath {
                    strokeColor: Qt.rgba(ring.colour.r, ring.colour.g, ring.colour.b, ring.strength)
                    strokeWidth: ring.stroke
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap
                    PathAngleArc {
                        centerX: 320; centerY: 320; radiusX: ring.radius; radiusY: ring.radius
                        startAngle: 0
                        sweepAngle: ring.dash / ring.radius * 180 / Math.PI
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
                centerX: 320; centerY: 320; centerRadius: 320
                focalX: 320; focalY: 320
                GradientStop { position: 0; color: "#0a1a2e" }
                GradientStop { position: 1; color: "#020308" }
            }
            PathRectangle { width: 640; height: 640 }
        }
    }

    TickRing {
        radius: 300; thickness: 10; dash: 3; period: 20.94; count: 90
        colour: Qt.rgba(1, 1, 1, 0.55)
        rotation: 360 * FaceMath.phase(face.time, 40, 0)
    }

    ArcRing {
        radius: 270; stroke: 18; glow: 47; count: 3; dash: 407.2; colour: "#2ec5ff"
        rotation: 360 * FaceMath.phase(face.time, 7, 0)
    }

    ArcRing {
        radius: 240; stroke: 6; glow: 16; count: 8; dash: 103.7; colour: "#a35bff"
        rotation: -360 * FaceMath.phase(face.time, 11, 0)
    }

    ArcRing {
        radius: 214; stroke: 14; glow: 36; count: 5; dash: 161.4; colour: "#2ec5ff"; strength: 0.85
        rotation: -360 * FaceMath.phase(face.time, 5, 0)
    }

    TickRing {
        radius: 188; thickness: 3; dash: 2; period: 19.69; count: 60
        colour: Qt.rgba(1, 1, 1, 0.7)
        rotation: 360 * FaceMath.phase(face.time, 24, 0)
    }

    FaceText {
        id: lead
        baseY: 240
        size: 32
        spacing: 5
        font.weight: 600
        color: "#2ec5ff"
        text: face.leadName
    }

    Reading {
        baseY: 360
        size: 134
        value: FaceMath.digits(face.cpuTemp)
        pair: face.pairValue
        label: lead
        pairScale: 0.54
        fitWidth: 320
    }

    FaceText {
        baseY: 410
        size: 32
        spacing: 2
        font.weight: 600
        color: "#bfe9ff"
        // There is no GPU load, so with the GPU leading the CPU's
        // temperature takes this line.
        text: face.has(face.cpuLoad) ? qsTr("LOAD %1%").arg(FaceMath.digits(face.cpuLoad))
            : face.onGpu && face.has(face.gpuTemp) ? qsTr("%1 %2°").arg(face.otherName).arg(FaceMath.digits(face.gpuTemp)) : ""
    }

    Mark {
        visible: face.showMark
        x: 308
        y: 438
        size: 24
        colour: "#2ec5ff"
    }
}
