import QtQuick
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real gpuTemp: number("gpuTemp", 61)
    readonly property real liquidTemp: number("liquidTemp", 34)
    // With one of the two not known, the other takes the middle and the
    // dot between them goes.
    readonly property bool lowerPair: has(gpuTemp) && has(liquidTemp) && !paired

    ShaderEffect {
        property real turn: FaceMath.phase(face.time, 7, 0)

        width: 640
        height: 640
        fragmentShader: Qt.resolvedUrl("shaders/rainbow.frag.qsb")
    }

    TickRing {
        radius: 246; thickness: 3; dash: 3; period: 16
        colour: Qt.rgba(1, 1, 1, 0.22)
        rotation: -360 * FaceMath.phase(face.time, 50, 0)
    }

    FaceText {
        id: lead
        baseY: 186
        size: 38
        spacing: 6
        font.weight: 600
        opacity: 0.85
        text: face.leadName
    }

    Reading {
        baseY: 396
        size: 228
        value: FaceMath.digits(face.cpuTemp)
        pair: face.pairValue
        label: lead
        fitWidth: 450
    }

    // The board centres each of these on its own point for a two digit
    // value. They are held by the edge nearest the dot instead, so a longer
    // value grows away from the dot rather than into it.
    TextMetrics {
        id: gpuSample
        font: gpu.font
        text: qsTr("%1 %2°").arg(face.otherName).arg(61)
    }

    TextMetrics {
        id: liquidSample
        font: liquid.font
        text: qsTr("LIQ %1°").arg(34)
    }

    FaceText {
        id: gpu
        visible: face.has(face.gpuTemp) && !face.paired
        centred: !face.lowerPair
        cx: face.lowerPair ? 224 + gpuSample.advanceWidth / 2 - width : 320
        baseY: 482
        size: 36
        font.weight: 600
        opacity: 0.9
        text: qsTr("%1 %2°").arg(face.otherName).arg(FaceMath.digits(face.gpuTemp))
    }

    Rectangle {
        visible: face.lowerPair
        x: 316
        y: 466
        width: 8
        height: 8
        radius: 4
        color: Qt.rgba(1, 1, 1, 0.4)
    }

    FaceText {
        id: liquid
        visible: face.has(face.liquidTemp)
        centred: !face.lowerPair
        cx: face.lowerPair ? 416 - liquidSample.advanceWidth / 2 : 320
        baseY: 482
        size: 36
        font.weight: 600
        opacity: 0.9
        text: qsTr("LIQ %1°").arg(FaceMath.digits(face.liquidTemp))
    }

    Mark {
        visible: face.showMark
        x: 300
        y: 532
        size: 40
    }
}
