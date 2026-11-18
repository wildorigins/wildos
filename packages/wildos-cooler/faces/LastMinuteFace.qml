import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    // One minute's line is one temperature's, so with both chosen this
    // face takes turns rather than standing two numbers over one line.
    canPair: false

    readonly property var history: list("cpuHistory", [49, 50, 50, 52, 51, 53, 56, 60, 63, 62, 58, 55, 54, 52,
                                                           51, 52, 55, 57, 56, 54, 53, 54, 56, 58, 57, 55, 54, 54])
    readonly property real cpuTemp: number("cpuTemp", history.length ? history[history.length - 1] : 54)
    readonly property real low: FaceMath.lowest(history, cpuTemp)
    readonly property real high: FaceMath.highest(history, cpuTemp)
    // With no temperature at all the board's own lines stand, with no
    // degrees written on them.
    readonly property var grid: has(low) ? FaceMath.gridFor(low, high) : ({ base: 50, step: 10 })

    readonly property color ink: themeColour("ink", "#ffffff")
    readonly property color lineStart: themeColour("accent", "#8fb4ff")
    readonly property color lineEnd: themeColour("accentDeep", "#f0a3ae")
    readonly property color lineMiddle: themed ? FaceMath.mix(lineStart, lineEnd, 0.5) : "#9d8ff0"
    readonly property color ground: themed ? Qt.rgba(lineStart.r * 0.016, lineStart.g * 0.016, lineStart.b * 0.016, 1) : "#05060a"
    readonly property color markColour: themed ? FaceMath.mix(lineStart, Qt.rgba(1, 1, 1, 1), 0.55) : "#b9c6ff"

    Rectangle {
        width: 640
        height: 640
        color: face.ground
    }

    Repeater {
        model: 3

        Item {
            id: line
            required property int index
            readonly property real lineY: 468 - 52 * index

            Shape {
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.10)
                    strokeWidth: 2
                    fillColor: "transparent"
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [2, 4]
                    capStyle: ShapePath.FlatCap
                    startX: 0; startY: line.lineY
                    PathLine { x: 640; y: line.lineY }
                }
            }

            FaceText {
                cx: 122
                baseY: line.lineY - 8
                size: 28
                font.weight: 550
                color: face.ink
                opacity: 0.6
                visible: face.has(face.low)
                text: qsTr("%1°").arg(face.grid.base + face.grid.step * line.index)
            }
        }
    }

    HistoryGraph {
        visible: face.history.length > 0
        values: face.history
        baseY: 468
        baseValue: face.grid.base
        perDegree: 52 / face.grid.step
        lineStart: face.lineStart
        lineMiddle: face.lineMiddle
        lineEnd: face.lineEnd
        fillColour: face.lineMiddle
        fillOpacity: 0.55
        haloColour: face.lineEnd
    }

    FaceText {
        baseY: 112
        size: 32
        spacing: 3
        font.weight: 600
        color: face.ink
        opacity: 0.85
        text: qsTr("%1, LAST MINUTE").arg(face.leadName)
    }

    Reading {
        baseY: 250
        size: 150
        colour: face.ink
        value: FaceMath.digits(face.cpuTemp)
    }

    FaceText {
        cx: 230
        baseY: 568
        size: 32
        font.weight: 550
        color: face.ink
        opacity: 0.9
        visible: face.has(face.low)
        text: qsTr("low %1°").arg(FaceMath.digits(face.low))
    }

    FaceText {
        cx: 412
        baseY: 568
        size: 32
        font.weight: 550
        color: face.ink
        opacity: 0.9
        visible: face.has(face.high)
        text: qsTr("high %1°").arg(FaceMath.digits(face.high))
    }

    Mark {
        visible: face.showMark
        x: 303
        y: 45
        size: 34
        colour: face.markColour
    }
}
