import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import "FaceMath.js" as FaceMath
import "MatrixData.js" as MatrixData

FaceBase {
    id: face

    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real gpuTemp: number("gpuTemp", 61)
    readonly property real liquidTemp: number("liquidTemp", 34)

    // The board asks for the system's monospace font and was drawn with
    // Consolas. On Wild OS that font is JetBrains Mono, which the Terminal
    // already brings. Its glyphs are wider and taller for the same size
    // (0.6 em against 0.55), so every size here is the board's times 0.917
    // and the text takes up the room the board gives it.
    readonly property string mono: "JetBrains Mono"
    readonly property real fit: 0.917
    // The board asks for weights 600 and 500. Consolas has neither and shows
    // bold and regular, so those are what the board looks like. Small light
    // glyphs on black also come out thinner here than in a browser, which
    // the falling glyphs make up for with one weight more.
    readonly property int heavy: 800
    readonly property int plain: 400
    readonly property real glyph: 37.14 * fit
    readonly property real row: 41.52
    // JetBrains Mono sits lower in its line than Consolas does.
    readonly property real lift: 6

    // The board fades each trail in from nothing at its top to full at its
    // foot with a mask. Here every glyph takes the mask's value at its own
    // row, which needs no mask at all.
    function trailMarkup(glyphs) {
        let out = ""
        const n = glyphs.length
        for (let i = 0; i < n; ++i) {
            const f = (i + 0.5) / n
            const a = f < 0.55 ? 0.45 * f / 0.55 : f < 0.96 ? 0.45 + 0.55 * (f - 0.55) / 0.41 : 1
            const alpha = Math.round(a * 255).toString(16).padStart(2, "0")
            out += "<font color=\"#" + alpha + "3dff7a\">" + glyphs[i] + "</font>" + (i < n - 1 ? "<br>" : "")
        }
        return out
    }

    // Where a column's top is: it falls from just above the face to past
    // the bottom, by 2.35 times its own height each cycle.
    // The cooler shows about 13 frames a second. At the board's speeds the
    // fastest columns drop most of a row between frames and flicker instead
    // of falling; this much slower, none moves more than 0.4 of a row.
    readonly property real slowdown: 2.25

    function fall(column) {
        const tall = (column.trail.length + 1) * row
        return -tall + 2.35 * tall * FaceMath.phase(face.time / slowdown, column.duration, column.delay) - lift
    }

    Rectangle {
        width: 640
        height: 640
        color: "#010402"
    }

    Repeater {
        model: MatrixData.columns

        Text {
            required property var modelData

            x: modelData.x
            y: face.fall(modelData)
            width: 40
            opacity: modelData.opacity
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.StyledText
            text: face.trailMarkup(modelData.trail)
            font.family: face.mono
            font.pixelSize: face.glyph
            font.weight: face.heavy
            lineHeightMode: Text.FixedHeight
            lineHeight: face.row
            renderType: Text.CurveRendering
        }
    }

    // The bright glyph at the foot of each column, all in one layer so that
    // one blur gives every one of them its glow.
    Item {
        width: 640
        height: 640
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#3dff7a"
            shadowBlur: 1
            blurMax: 48
        }

        Repeater {
            model: MatrixData.columns

            Text {
                required property var modelData

                x: modelData.x
                y: face.fall(modelData) + modelData.trail.length * face.row
                width: 40
                opacity: modelData.opacity
                horizontalAlignment: Text.AlignHCenter
                text: modelData.head
                color: "#eafff0"
                font.family: face.mono
                font.pixelSize: face.glyph
                font.weight: face.heavy
                lineHeightMode: Text.FixedHeight
                lineHeight: face.row
                renderType: Text.CurveRendering
            }
        }
    }

    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 320; centerY: 330; centerRadius: 262
                focalX: 320; focalY: 330
                GradientStop { position: 0; color: Qt.rgba(1 / 255, 4 / 255, 2 / 255, 1) }
                GradientStop { position: 0.72; color: Qt.rgba(1 / 255, 4 / 255, 2 / 255, 0.94) }
                GradientStop { position: 1; color: Qt.rgba(1 / 255, 4 / 255, 2 / 255, 0) }
            }
            PathAngleArc { centerX: 320; centerY: 330; radiusX: 262; radiusY: 262; startAngle: 0; sweepAngle: 360 }
        }
    }

    FaceText {
        id: lead
        baseY: 214
        size: 38 * face.fit
        spacing: 6
        font.family: face.mono
        font.weight: face.heavy
        color: "#3dff7a"
        text: face.leadName
    }

    Item {
        width: 640
        height: 640
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(61 / 255, 1, 122 / 255, 0.55)
            shadowBlur: 1
            shadowOpacity: 0.85
            blurMax: 64
        }

        Reading {
            id: temperature
            cx: 306
            baseY: 386
            size: 190 * face.fit
            weight: face.plain
            spacing: -8
            family: face.mono
            colour: "#eafff0"
            ringShown: false
            degreeOffset: 130
            degreeRise: 124
            degreeRadius: 13
            degreeStroke: 6
            value: FaceMath.digits(face.cpuTemp)
            pair: face.pairValue
            label: lead
            fitWidth: 400
        }
    }

    Rectangle {
        visible: temperature.value !== "" && !temperature.paired
        x: temperature.cx + (temperature.ringX - temperature.cx) * temperature.fit - width / 2
        y: 386 - temperature.size * 0.35 - (124 - temperature.size * 0.35) * temperature.fit - height / 2
        width: 32 * temperature.fit
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 6 * temperature.fit
        border.color: "#eafff0"
    }

    FaceText {
        baseY: 462
        size: 36 * face.fit
        spacing: 1
        font.family: face.mono
        font.weight: face.heavy
        color: "#3dff7a"
        text: face.joined([face.has(face.liquidTemp) ? qsTr("liq %1").arg(FaceMath.digits(face.liquidTemp)) : "",
                           face.has(face.gpuTemp) && !face.paired ? qsTr("%1 %2").arg(face.otherName.toLowerCase()).arg(FaceMath.digits(face.gpuTemp)) : ""], qsTr(" / "))
    }

    Mark {
        visible: face.showMark
        x: 300
        y: 518
        size: 40
        colour: "#3dff7a"
    }
}
