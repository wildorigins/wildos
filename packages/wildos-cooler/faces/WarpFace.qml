import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath
import "WarpData.js" as WarpData

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real gpuTemp: number("gpuTemp", 61)
    readonly property real liquidTemp: number("liquidTemp", 34)

    // Fourteen sheets of streaks, each growing from half size to double and
    // fading as it goes. The streaks are laid once; a frame only changes the
    // fourteen scales and opacities.
    Repeater {
        model: WarpData.groups

        Item {
            id: sheet
            required property var modelData
            readonly property real flight: FaceMath.phase(face.time, modelData.duration, modelData.delay)

            width: 640
            height: 640
            scale: 0.5 + 1.5 * flight
            opacity: FaceMath.keyframes(flight, [[0, 0], [0.18, 1], [1, 0]], FaceMath.linear)

            Repeater {
                model: sheet.modelData.streaks

                Item {
                    id: streak
                    required property var modelData

                    Streak {
                        x1: streak.modelData[0]
                        y1: streak.modelData[1]
                        x2: streak.modelData[2]
                        y2: streak.modelData[3]
                        thickness: streak.modelData[6] * 2.6
                        color: Qt.hsla(streak.modelData[4] / 360, 1, 0.62, streak.modelData[5] * 0.355)
                    }

                    Streak {
                        x1: streak.modelData[0]
                        y1: streak.modelData[1]
                        x2: streak.modelData[2]
                        y2: streak.modelData[3]
                        thickness: streak.modelData[6]
                        color: Qt.hsla(streak.modelData[4] / 360, 1, 0.72, streak.modelData[5])
                    }
                }
            }
        }
    }

    // The board masks the streaks away towards the middle. Over a black
    // ground that is the same as black laid on top, fading out with distance.
    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 320; centerY: 320; centerRadius: 320
                focalX: 320; focalY: 320
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 1) }
                GradientStop { position: 0.36; color: Qt.rgba(0, 0, 0, 1) }
                GradientStop { position: 0.62; color: Qt.rgba(0, 0, 0, 0) }
            }
            PathRectangle { width: 640; height: 640 }
        }
    }

    Rectangle {
        x: 149
        y: 149
        width: 342
        height: 342
        radius: 171
        color: Qt.rgba(11 / 255, 13 / 255, 12 / 255, 0.94)
        border.width: 2
        border.color: Qt.rgba(1, 1, 1, 0.14)
    }

    Shape {
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Qt.rgba(1, 1, 1, 0.12)
            strokeWidth: 6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: 320; centerY: 320; radiusX: 170; radiusY: 170; startAngle: 135; sweepAngle: 270 }
        }

        ShapePath {
            strokeColor: "#3dff7a"
            strokeWidth: 6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: 320; centerY: 320; radiusX: 170; radiusY: 170
                startAngle: 135
                sweepAngle: 270 * FaceMath.heat(face.cpuTemp)
            }
        }
    }

    FaceText {
        id: lead
        baseY: 222
        size: 34
        spacing: 5
        font.weight: 600
        color: "#3dff7a"
        text: face.leadName
    }

    Reading {
        baseY: 352
        size: 150
        value: FaceMath.digits(face.cpuTemp)
        pair: face.pairValue
        label: lead
        pairScale: 0.54
        fitWidth: 300
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
        colour: "#3dff7a"
    }
}
