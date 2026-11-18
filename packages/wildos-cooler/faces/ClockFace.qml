import QtQuick
import QtQuick.Shapes
import "FaceMath.js" as FaceMath

FaceBase {
    id: face

    // 24 hour, as the board shows it. The app sets this from the clock
    // format chosen in Settings.
    property string timeFormat: "HH:mm"

    readonly property date now: reading("now", new Date(2026, 10, 17, 14, 32)) ?? new Date()
    readonly property real cpuTemp: number("cpuTemp", 54)
    readonly property real liquidTemp: number("liquidTemp", 34)

    readonly property color ink: themeColour("ink", "#ffffff")
    readonly property color accent: themeColour("accent", "#4f7dff")
    readonly property color accentDeep: themeColour("accentDeep", "#a45cf0")
    readonly property color ground: themed ? Qt.rgba(accent.r * 0.035, accent.g * 0.035, accent.b * 0.035, 1) : "#060710"
    readonly property color firstGlow: themed ? Qt.rgba(accent.r * 0.75, accent.g * 0.75, accent.b * 0.75, 1) : "#4f7dff"
    readonly property color secondGlow: accentDeep
    readonly property color thirdGlow: themed ? FaceMath.mix(accent, accentDeep, 0.5) : "#f0778f"

    Rectangle {
        width: 640
        height: 640
        color: face.ground
    }

    Shape {
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 150; centerY: 170; centerRadius: 330
                focalX: 150; focalY: 170
                GradientStop { position: 0; color: FaceMath.alpha(face.firstGlow, 0.75) }
                GradientStop { position: 1; color: FaceMath.alpha(face.firstGlow, 0) }
            }
            PathRectangle { width: 640; height: 640 }
        }

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 520; centerY: 250; centerRadius: 300
                focalX: 520; focalY: 250
                GradientStop { position: 0; color: FaceMath.alpha(face.secondGlow, 0.6) }
                GradientStop { position: 1; color: FaceMath.alpha(face.secondGlow, 0) }
            }
            PathRectangle { width: 640; height: 640 }
        }

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 330; centerY: 600; centerRadius: 330
                focalX: 330; focalY: 600
                GradientStop { position: 0; color: FaceMath.alpha(face.thirdGlow, 0.6) }
                GradientStop { position: 1; color: FaceMath.alpha(face.thirdGlow, 0) }
            }
            PathRectangle { width: 640; height: 640 }
        }
    }

    FaceText {
        baseY: 204
        size: 36
        font.weight: 500
        color: face.ink
        opacity: 0.85
        text: Qt.locale().toString(face.now, "dddd d MMMM")
    }

    FaceText {
        baseY: 372
        size: 196
        spacing: -7
        font.weight: 300
        color: face.ink
        text: Qt.locale().toString(face.now, face.timeFormat)
    }

    // One pill for each known reading, centred as a group: the board's
    // two side by side, one alone in the middle, three when both
    // temperatures are on.
    readonly property var pills: [
        has(cpuTemp) ? qsTr("%1° %2").arg(FaceMath.digits(cpuTemp)).arg(leadName.toLowerCase()) : "",
        paired ? qsTr("%1° %2").arg(pairValue).arg(qsTr("GPU").toLowerCase()) : "",
        has(liquidTemp) ? qsTr("%1° liq").arg(FaceMath.digits(liquidTemp)) : ""
    ].filter(text => text !== "")
    readonly property var pillCentres: pills.length === 3 ? [150, 320, 490] : pills.length === 2 ? [218, 422] : [320]

    Repeater {
        model: face.pills

        Item {
            required property string modelData
            required property int index
            readonly property real centre: face.pillCentres[index]

            Rectangle {
                x: parent.centre - width / 2
                y: 430
                width: face.pills.length === 3 ? 168 : 180
                height: 72
                radius: 36
                color: Qt.rgba(0, 0, 0, 0.34)
            }

            FaceText {
                cx: parent.centre
                baseY: 479
                size: 36
                font.weight: 500
                color: face.ink
                text: parent.modelData
            }
        }
    }

    Mark {
        visible: face.showMark
        x: 300
        y: 542
        size: 40
        colour: face.ink
    }
}
