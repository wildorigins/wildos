import QtQuick
import QtQuick.Shapes

// What every face shares: the 640 unit drawing space, the round edge, the
// sheen the board lays over each face, and the properties the app and the
// renderer drive. A face puts its drawing inside as children and positions
// it in board units, whatever size the face itself is given.
Item {
    id: face

    property real time: 0
    // null draws the board's own example. An object is the truth: whatever
    // it leaves out, or gives as null or below nought, is not known and
    // is left off the face.
    property var readings: null
    property bool showMark: true
    property var themeColours: null
    // The cooler's own screen is round, so the renderer that feeds it can
    // turn this off and save the extra pass.
    property bool circular: true

    readonly property bool themed: themeColours !== null && themeColours !== undefined
    readonly property real unit: width / 640

    default property alias content: stage.data

    readonly property bool exampleOnly: readings === null || readings === undefined

    // Whose temperature leads, from the reading "show": the CPU, the GPU, or
    // both. With the GPU chosen its name leads even before its first reading
    // comes, and its number stays off until then. With both chosen the
    // CPU's and the GPU's numbers stand side by side (paired, through
    // Reading); a face that cannot hold two (canPair off) takes turns every
    // five seconds instead. Without a GPU reading, both is the CPU alone. A
    // face about the CPU alone (its cores, its limit) or about both at once
    // turns followsChoice off.
    property bool followsChoice: true
    property bool canPair: true
    readonly property string choice: exampleOnly ? "cpu" : (readings["show"] ?? "cpu")
    readonly property bool gpuKnown: !exampleOnly && typeof readings["gpuTemp"] === "number" && readings["gpuTemp"] >= 0
    readonly property bool paired: followsChoice && canPair && choice === "both" && gpuKnown
    readonly property bool onGpu: followsChoice && (choice === "gpu"
                                  || (choice === "both" && !canPair && gpuKnown && Math.floor(time / 5) % 2 === 1))
    // The GPU's number for the second place of a paired Reading.
    readonly property string pairValue: paired ? Math.round(readings["gpuTemp"]).toString() : ""
    readonly property string leadName: onGpu ? qsTr("GPU") : qsTr("CPU")
    readonly property string otherName: onGpu ? qsTr("CPU") : qsTr("GPU")
    // With the GPU leading, a face asking for the CPU's readings is given
    // the GPU's, and the other way round. There is no GPU load or highest
    // of the day, so those are left off.
    readonly property var swapped: ({
        cpuTemp: "gpuTemp", gpuTemp: "cpuTemp",
        cpuHistory: "gpuHistory", gpuHistory: "cpuHistory",
        cpuHighest: "gpuHighest", cpuLoad: "gpuLoad"
    })

    // A reading of any kind, or undefined when it is not known.
    function reading(name, example) {
        if (face.exampleOnly)
            return example
        const v = face.readings[face.onGpu && swapped[name] !== undefined ? swapped[name] : name]
        return v === null ? undefined : v
    }

    // A temperature, speed or load. Not known is NaN, which has() tells and
    // FaceMath.digits() writes as nothing; a face never shows a number it
    // was not given.
    function number(name, example) {
        const v = reading(name, example)
        return typeof v === "number" && isFinite(v) && v >= 0 ? v : NaN
    }

    function list(name, example) {
        const v = reading(name, example)
        return v !== undefined && v !== null && v.length !== undefined ? v : []
    }

    function has(value) {
        return !isNaN(value)
    }

    // The parts that are there, with the separator between them only.
    function joined(parts, separator) {
        return parts.filter(part => part !== "").join(separator)
    }

    // A theme colour, or the board's own when no theme is set.
    function themeColour(name, own) {
        if (!face.themed)
            return own
        const v = face.themeColours[name]
        return v === undefined || v === null ? own : v
    }

    implicitWidth: 640
    implicitHeight: 640

    Item {
        anchors.fill: parent
        clip: !face.circular
        layer.enabled: face.circular
        layer.smooth: true
        layer.effect: ShaderEffect {
            property real feather: 3 / Math.max(1, face.width)
            fragmentShader: Qt.resolvedUrl("shaders/round.frag.qsb")
        }

        Rectangle {
            anchors.fill: parent
            color: "#000000"
        }

        Item {
            width: 640
            height: 640
            scale: face.unit
            transformOrigin: Item.TopLeft

            Item {
                id: stage
                anchors.fill: parent
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: "transparent"
                    fillGradient: LinearGradient {
                        x1: 30.2; y1: -25.3
                        x2: 609.8; y2: 665.3
                        GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.07) }
                        GradientStop { position: 0.38; color: Qt.rgba(1, 1, 1, 0) }
                    }
                    PathRectangle { width: 640; height: 640 }
                }
            }
        }
    }
}
