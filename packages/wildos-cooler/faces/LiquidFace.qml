import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath
import "LiquidData.js" as LiquidData

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real liquidTemp: number("liquidTemp", 34)
    readonly property real pumpRpm: number("pumpRpm", 2100)
    // With one of the two lower readings not known, the other takes the middle.
    readonly property bool lowerPair: has(liquidTemp) && has(pumpRpm)

    readonly property color ink: themeColour("ink", "#ffffff")
    readonly property color first: themeColour("accent", "#7fb0ff")
    readonly property color third: themeColour("accentDeep", "#f08fb0")
    readonly property color second: themed ? FaceMath.mix(first, third, 0.5) : "#8d7df0"
    readonly property color glowCentre: themed ? Qt.rgba(first.r * 0.095, first.g * 0.095, first.b * 0.095, 1) : "#1a2140"
    readonly property color glowEdge: themed ? Qt.rgba(first.r * 0.016, first.g * 0.016, first.b * 0.016, 1) : "#05060b"

    // The board lays a white soft-light wash over the front wave, strongest
    // at the crest. The wave under it is opaque and both run top to bottom,
    // so the blend is worked out once per stop instead of in a shader.
    function crest(f) {
        const base = FaceMath.gradientAt(first, second, third, 0.5, f)
        const wash = 0.5 * (1 - f) * 0.35
        const lift = c => c <= 0.25 ? ((16 * c - 12) * c + 4) * c : Math.sqrt(c)
        return Qt.rgba(base.r + (lift(base.r) - base.r) * wash,
                       base.g + (lift(base.g) - base.g) * wash,
                       base.b + (lift(base.b) - base.b) * wash, 1)
    }

    Shape {
        width: 640
        height: 640
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 320; centerY: 192; centerRadius: 480
                focalX: 320; focalY: 192
                GradientStop { position: 0; color: face.glowCentre }
                GradientStop { position: 1; color: face.glowEdge }
            }
            PathRectangle { width: 640; height: 640 }
        }
    }

    Shape {
        x: FaceMath.phase(face.time, 11, 0) * 420
        opacity: 0.38
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 289; x2: 0; y2: 640
                GradientStop { position: 0; color: face.first }
                GradientStop { position: 0.5; color: face.second }
                GradientStop { position: 1; color: face.third }
            }
            PathSvg { path: LiquidData.backWave }
        }
    }

    Shape {
        x: FaceMath.phase(face.time, 6, 0) * 300
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 307; x2: 0; y2: 640
                GradientStop { position: 0; color: face.crest(0) }
                GradientStop { position: 0.125; color: face.crest(0.125) }
                GradientStop { position: 0.25; color: face.crest(0.25) }
                GradientStop { position: 0.375; color: face.crest(0.375) }
                GradientStop { position: 0.5; color: face.crest(0.5) }
                GradientStop { position: 0.625; color: face.crest(0.625) }
                GradientStop { position: 0.75; color: face.crest(0.75) }
                GradientStop { position: 0.875; color: face.crest(0.875) }
                GradientStop { position: 1; color: face.crest(1) }
            }
            PathSvg { path: LiquidData.frontWave }
        }
    }

    Repeater {
        // Centre x, centre y, radius, opacity, seconds per rise, CSS delay.
        model: [
            [150, 520, 9, 0.16, 4, 0], [188, 458, 5, 0.2, 7, -2], [232, 560, 6, 0.14, 6, 0],
            [438, 498, 10, 0.14, 7, -2], [470, 430, 5, 0.2, 4, -2], [398, 566, 7, 0.13, 7, -2],
            [300, 600, 5, 0.16, 4, 0], [512, 540, 6, 0.12, 6, 0], [120, 440, 4, 0.18, 4, 0]
        ]

        Rectangle {
            required property var modelData
            readonly property real rise: FaceMath.phase(face.time, modelData[4], modelData[5])

            x: modelData[0] - modelData[2]
            y: modelData[1] - modelData[2] + 30 - 110 * FaceMath.easeIn(rise)
            width: modelData[2] * 2
            height: width
            radius: modelData[2]
            color: "#ffffff"
            opacity: modelData[3] * FaceMath.keyframes(rise, [[0, 0], [0.2, 1], [1, 0]], FaceMath.easeIn)
        }
    }

    Shape {
        opacity: 0.9
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            fillGradient: LinearGradient {
                x1: 60; y1: 0; x2: 580; y2: 0
                GradientStop { position: 0; color: face.first }
                GradientStop { position: 0.55; color: face.second }
                GradientStop { position: 1; color: face.third }
            }
            PathAngleArc { centerX: 320; centerY: 320; radiusX: 306.5; radiusY: 306.5; startAngle: 0; sweepAngle: 360 }
            PathAngleArc { centerX: 320; centerY: 320; radiusX: 301.5; radiusY: 301.5; startAngle: 0; sweepAngle: 360; moveToStart: true }
        }
    }

    FaceText {
        id: lead
        baseY: 150
        size: 38
        spacing: 6
        font.weight: 600
        color: face.ink
        opacity: 0.85
        text: face.leadName
    }

    Reading {
        baseY: 372
        size: 236
        colour: face.ink
        value: FaceMath.digits(face.cpuTemp)
        pair: face.pairValue
        label: lead
        fitWidth: 520
    }

    FaceText {
        visible: face.has(face.liquidTemp)
        cx: face.lowerPair ? 206 : 320
        baseY: 506
        size: 50
        font.weight: 500
        color: face.ink
        text: qsTr("%1°").arg(FaceMath.digits(face.liquidTemp))
    }

    FaceText {
        visible: face.has(face.liquidTemp)
        cx: face.lowerPair ? 206 : 320
        baseY: 546
        size: 32
        font.weight: 550
        color: face.ink
        opacity: 0.9
        text: qsTr("liquid")
    }

    Rectangle {
        visible: face.lowerPair
        x: 319
        y: 458
        width: 2
        height: 96
        color: face.ink
        opacity: 0.3
    }

    FaceText {
        visible: face.has(face.pumpRpm)
        cx: face.lowerPair ? 434 : 320
        baseY: 506
        size: 50
        font.weight: 500
        color: face.ink
        text: face.has(face.pumpRpm) ? Number(Math.round(face.pumpRpm)).toLocaleString(Qt.locale(), "f", 0) : ""
    }

    FaceText {
        visible: face.has(face.pumpRpm)
        cx: face.lowerPair ? 434 : 320
        baseY: 546
        size: 32
        font.weight: 550
        color: face.ink
        opacity: 0.9
        text: qsTr("pump rpm")
    }

    Mark {
        visible: face.showMark
        x: 301
        y: 585
        size: 38
        colour: face.ink
    }
}
