import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real gpuTemp: number("gpuTemp", 61)
    readonly property real cpuLoad: number("cpuLoad", 23)
    // 55 ticks, 5 degrees apart, round three quarters of the rim. The board
    // lights the first 20 at 54 degrees: every tick up to the temperature.
    readonly property int litTicks: Math.round(54 * FaceMath.heat(cpuTemp)) + 1

    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 320; centerY: 384; centerRadius: 500
                focalX: 320; focalY: 384
                GradientStop { position: 0; color: "#06260f" }
                GradientStop { position: 0.62; color: "#020503" }
                GradientStop { position: 1; color: "#020503" }
            }
            PathRectangle { width: 640; height: 640 }
        }
    }

    ShaderEffect {
        property real turn: FaceMath.phase(face.time, 3.2, 0)
        property color tint: "#3dff7a"

        width: 640
        height: 640
        fragmentShader: Qt.resolvedUrl("shaders/sweep.frag.qsb")
    }

    Repeater {
        model: 55

        Streak {
            required property int index
            readonly property real bearing: (135 + index * 5) * Math.PI / 180
            readonly property bool lit: index < face.litTicks

            x1: 320 + 270 * Math.cos(bearing)
            y1: 320 + 270 * Math.sin(bearing)
            x2: 320 + 302 * Math.cos(bearing)
            y2: 320 + 302 * Math.sin(bearing)
            thickness: 7
            color: lit ? "#3dff7a" : Qt.rgba(1, 1, 1, 0.12)
            // Lit ticks dim and recover one after another, a twentieth of a
            // second apart. Unlit ones never read the time.
            opacity: lit ? FaceMath.keyframes(FaceMath.phase(face.time, 2.6, -0.05 * index),
                                              [[0, 1], [0.5, 0.55], [1, 1]], FaceMath.easeInOut)
                         : 1
        }
    }

    FaceText {
        id: lead
        baseY: 186
        size: 38
        spacing: 6
        font.weight: 600
        color: "#3dff7a"
        text: face.leadName
    }

    Reading {
        baseY: 396
        size: 228
        value: FaceMath.digits(face.cpuTemp)
        pair: face.pairValue
        label: lead
        fitWidth: 470
    }

    Rectangle {
        visible: face.has(face.cpuLoad)
        x: 210
        y: 452
        width: 220
        height: 10
        radius: 5
        color: Qt.rgba(1, 1, 1, 0.12)
    }

    Rectangle {
        x: 210
        y: 452
        width: Math.max(10, 220 * FaceMath.clamp(face.cpuLoad / 100, 0, 1))
        height: 10
        radius: 5
        color: "#3dff7a"
        visible: face.cpuLoad > 0
    }

    FaceText {
        baseY: 510
        size: 36
        spacing: 2
        font.weight: 600
        opacity: 0.9
        // There is no GPU load, so with the GPU leading the CPU's
        // temperature takes this line.
        text: face.has(face.cpuLoad) ? qsTr("LOAD %1%").arg(FaceMath.digits(face.cpuLoad))
            : face.onGpu && face.has(face.gpuTemp) ? qsTr("%1 %2°").arg(face.otherName).arg(FaceMath.digits(face.gpuTemp)) : ""
    }

    Mark {
        visible: face.showMark
        x: 301
        y: 552
        size: 36
        colour: "#3dff7a"
    }
}
