import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    followsChoice: false

    readonly property var cpuHistory: list("cpuHistory", [49, 50, 50, 52, 51, 53, 56, 60, 63, 62, 58, 55, 54, 52,
                                                              51, 52, 55, 57, 56, 54, 53, 54, 56, 58, 57, 55, 54, 54])
    readonly property var gpuHistory: list("gpuHistory", [58, 58, 59, 59, 60, 61, 61, 62, 64, 66, 67, 66, 65, 63,
                                                              62, 62, 61, 61, 62, 63, 63, 62, 61, 61, 62, 62, 61, 61])
    readonly property real cpuTemp: number("cpuTemp", cpuHistory.length ? cpuHistory[cpuHistory.length - 1] : 54)
    readonly property real gpuTemp: number("gpuTemp", gpuHistory.length ? gpuHistory[gpuHistory.length - 1] : 61)
    // The scale comes from whichever of the two is known; with neither,
    // the board's own lines stand.
    readonly property var known: [FaceMath.lowest(cpuHistory, cpuTemp), FaceMath.highest(cpuHistory, cpuTemp),
                                  FaceMath.lowest(gpuHistory, gpuTemp), FaceMath.highest(gpuHistory, gpuTemp)].filter(v => has(v))
    readonly property var grid: known.length ? FaceMath.gridFor(Math.min(...known), Math.max(...known)) : ({ base: 50, step: 10 })

    readonly property color ink: themeColour("ink", "#ffffff")
    readonly property color cpuColour: themeColour("accent", "#8fb4ff")
    readonly property color gpuColour: themeColour("accentDeep", "#f08fb0")
    readonly property color ground: themed ? Qt.rgba(cpuColour.r * 0.016, cpuColour.g * 0.016, cpuColour.b * 0.016, 1) : "#05060a"
    readonly property color markColour: themed ? FaceMath.mix(cpuColour, Qt.rgba(1, 1, 1, 1), 0.55) : "#b9c6ff"

    Rectangle {
        width: 640
        height: 640
        color: face.ground
    }

    Repeater {
        model: 3

        Shape {
            id: line
            required property int index
            readonly property real lineY: 496 - 64 * index

            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Qt.rgba(1, 1, 1, 0.09)
                strokeWidth: 2
                fillColor: "transparent"
                strokeStyle: ShapePath.DashLine
                dashPattern: [2, 4]
                capStyle: ShapePath.FlatCap
                startX: 0; startY: line.lineY
                PathLine { x: 640; y: line.lineY }
            }
        }
    }

    HistoryGraph {
        visible: face.gpuHistory.length > 0
        values: face.gpuHistory
        baseY: 496
        baseValue: face.grid.base
        perDegree: 64 / face.grid.step
        lineStart: face.gpuColour
        fillOpacity: 0.35
        dotColour: face.gpuColour
    }

    HistoryGraph {
        visible: face.cpuHistory.length > 0
        values: face.cpuHistory
        baseY: 496
        baseValue: face.grid.base
        perDegree: 64 / face.grid.step
        lineStart: face.cpuColour
        fillOpacity: 0.45
        dotColour: face.cpuColour
    }

    FaceText {
        baseY: 112
        size: 32
        spacing: 4
        font.weight: 600
        color: face.ink
        opacity: 0.85
        text: qsTr("LAST MINUTE")
    }

    Rectangle {
        x: 160
        y: 182
        width: 22
        height: 22
        radius: 11
        color: face.cpuColour
    }

    FaceText {
        cx: 192
        centred: false
        baseY: 208
        size: 34
        font.weight: 600
        color: face.ink
        opacity: 0.9
        text: qsTr("CPU")
    }

    Reading {
        cx: 222
        baseY: 290
        size: 96
        weight: 300
        colour: face.ink
        value: FaceMath.digits(face.cpuTemp)
        fitWidth: 190
    }

    Rectangle {
        x: 370
        y: 182
        width: 22
        height: 22
        radius: 11
        color: face.gpuColour
    }

    FaceText {
        cx: 402
        centred: false
        baseY: 208
        size: 34
        font.weight: 600
        color: face.ink
        opacity: 0.9
        text: qsTr("GPU")
    }

    Reading {
        cx: 432
        baseY: 290
        size: 96
        weight: 300
        colour: face.ink
        value: FaceMath.digits(face.gpuTemp)
        sample: "61"
        fitWidth: 190
    }

    Mark {
        visible: face.showMark
        x: 303
        y: 45
        size: 34
        colour: face.markColour
    }
}
