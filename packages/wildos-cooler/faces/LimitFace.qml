import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

// The red warning. The cooler switches to it by itself; it is not offered
// with the other faces and keeps its red whatever the theme.
FaceBase {
    id: face

    followsChoice: false

    readonly property real cpuTemp: number("cpuTemp", 99)
    readonly property real liquidTemp: number("liquidTemp", 52)
    readonly property string reason: reading("limitReason", undefined) ?? qsTr("At its limit")
    // 48 ticks all the way round. The board leaves the last two dim even at
    // 99 degrees, so the ring always shows where it begins.
    readonly property int litTicks: Math.min(46, Math.round(48 * FaceMath.heat(cpuTemp)))

    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 320; centerY: 352; centerRadius: 384
                focalX: 320; focalY: 352
                GradientStop { position: 0; color: "#6a0d12" }
                GradientStop { position: 1; color: "#0d0203" }
            }
            PathRectangle { width: 640; height: 640 }
        }
    }

    Repeater {
        model: 48

        Streak {
            required property int index
            readonly property real bearing: (index * 7.5 - 90) * Math.PI / 180

            x1: 320 + 274 * Math.cos(bearing)
            y1: 320 + 274 * Math.sin(bearing)
            x2: 320 + 304 * Math.cos(bearing)
            y2: 320 + 304 * Math.sin(bearing)
            thickness: 12
            color: "#ff5b5b"
            opacity: index < face.litTicks ? 1 : 0.22
        }
    }

    Shape {
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "#ff5b5b"
            strokeWidth: 7
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M320 112 l34 60 h-68z" }
        }

        ShapePath {
            strokeColor: "#ff5b5b"
            strokeWidth: 7
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg { path: "M320 136 v18" }
        }
    }

    Rectangle {
        x: 316
        y: 159
        width: 8
        height: 8
        radius: 4
        color: "#ff5b5b"
    }

    FaceText {
        baseY: 220
        size: 38
        spacing: 5
        font.weight: 600
        color: "#ff8a8a"
        text: qsTr("CPU")
    }

    Reading {
        baseY: 410
        size: 224
        colour: "#ffd0d0"
        sample: "99"
        value: FaceMath.digits(face.cpuTemp)
        fitWidth: 470
    }

    FaceText {
        id: reasonText
        baseY: 482
        size: 38
        font.weight: 500
        color: "#ff8a8a"
        text: face.reason
        // A longer reason, or a longer translation, is kept inside the face.
        scale: Math.min(1, 420 / Math.max(1, implicitWidth))
        transformOrigin: Item.Bottom
    }

    FaceText {
        baseY: 530
        size: 34
        font.weight: 550
        opacity: 0.9
        visible: face.has(face.liquidTemp)
        text: qsTr("liquid %1°").arg(FaceMath.digits(face.liquidTemp))
    }

    Mark {
        visible: face.showMark
        x: 302
        y: 556
        size: 34
        colour: "#ff8a8a"
    }
}
