import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    followsChoice: false

    readonly property var coreLoads: list("coreLoads", [62, 18, 41, 9, 27, 73, 12, 33, 8, 22, 5, 14, 11, 6, 19, 8])
    readonly property real cpuLoad: number("cpuLoad", coreLoads.length
                                            ? coreLoads.reduce((a, b) => a + b, 0) / coreLoads.length : 23)
    readonly property real cpuTemp: number("cpuTemp", 54)

    // The board's sixteen bars are 20 wide and 31 apart. More cores than
    // that share the same 485 units, each bar and gap narrower.
    readonly property int cores: coreLoads.length
    readonly property real pitch: Math.min(31, 485 / Math.max(1, cores - 1 + 20 / 31))
    readonly property real barWidth: pitch * 20 / 31
    readonly property real firstX: 320 - (pitch * (cores - 1) + barWidth) / 2

    readonly property color ink: themeColour("ink", "#ffffff")
    readonly property color low: themeColour("accent", "#7fb0ff")
    readonly property color high: themeColour("accentDeep", "#f08fb0")
    readonly property color mid: themed ? FaceMath.mix(low, high, 0.5) : "#9d8ff0"
    readonly property color glowCentre: themed ? Qt.rgba(low.r * 0.095, low.g * 0.095, low.b * 0.095, 1) : "#151a33"
    readonly property color glowEdge: themed ? Qt.rgba(low.r * 0.016, low.g * 0.016, low.b * 0.016, 1) : "#05060a"
    readonly property color markColour: themed ? FaceMath.mix(low, Qt.rgba(1, 1, 1, 1), 0.55) : "#b9c6ff"

    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 320; centerY: 224; centerRadius: 448
                focalX: 320; focalY: 224
                GradientStop { position: 0; color: face.glowCentre }
                GradientStop { position: 1; color: face.glowEdge }
            }
            PathRectangle { width: 640; height: 640 }
        }
    }

    FaceText {
        baseY: 114
        size: 32
        spacing: 4
        font.weight: 600
        color: face.ink
        opacity: 0.85
        text: qsTr("CPU LOAD")
    }

    Reading {
        id: load
        cx: 306
        baseY: 252
        size: 150
        colour: face.ink
        degree: false
        sample: "23"
        value: FaceMath.digits(face.cpuLoad)
    }

    FaceText {
        cx: 384 + load.grown
        centred: false
        baseY: 252
        size: 54
        font.weight: 350
        color: face.ink
        opacity: 0.7
        visible: face.has(face.cpuLoad)
        text: qsTr("%")
    }

    Repeater {
        model: face.cores

        Item {
            id: bar
            required property int index
            readonly property real share: FaceMath.clamp(face.coreLoads[index] / 100, 0, 1)
            // The shortest bar is a half-height dot, as the board's lowest core is.
            readonly property real barHeight: Math.max(face.barWidth / 2, share * 190)
            readonly property real reach: barHeight / 190

            x: face.firstX + face.pitch * index
            y: 302
            width: face.barWidth
            height: 190

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Qt.rgba(1, 1, 1, 0.07)
            }

            // Every bar shows its own slice of one gradient that runs the
            // full height of the tracks, so tall bars turn pink at the top.
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: bar.barHeight
                radius: Math.min(width, height) / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: FaceMath.gradientAt(face.low, face.mid, face.high, 0.55, bar.reach) }
                    GradientStop {
                        position: bar.reach > 0.55 ? 1 - 0.55 / bar.reach : 0
                        color: bar.reach > 0.55 ? face.mid : FaceMath.gradientAt(face.low, face.mid, face.high, 0.55, bar.reach)
                    }
                    GradientStop { position: 1; color: face.low }
                }
            }
        }
    }

    FaceText {
        baseY: 552
        size: 36
        font.weight: 550
        color: face.ink
        opacity: 0.9
        text: face.cores > 0 && face.has(face.cpuTemp) ? qsTr("%n cores · %1°", "", face.cores).arg(FaceMath.digits(face.cpuTemp))
            : face.cores > 0 ? qsTr("%n cores", "", face.cores)
            : face.has(face.cpuTemp) ? qsTr("%1°").arg(FaceMath.digits(face.cpuTemp)) : ""
    }

    Mark {
        visible: face.showMark
        x: 303
        y: 575
        size: 34
        colour: face.markColour
    }
}
