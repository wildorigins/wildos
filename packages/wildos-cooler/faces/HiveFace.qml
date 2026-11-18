import QtQuick
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real gpuTemp: number("gpuTemp", 61)
    readonly property real liquidTemp: number("liquidTemp", 34)

    ShaderEffect {
        // The ripple repeats every 2.6 seconds; keeping the number small
        // keeps it exact in the shader however long the PC has been on.
        property real time: FaceMath.wrap(face.time, 2.6)

        width: 640
        height: 640
        fragmentShader: Qt.resolvedUrl("shaders/hive.frag.qsb")
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
        size: 142
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
