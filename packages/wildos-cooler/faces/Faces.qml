pragma Singleton
import QtQuick

// The faces, in the order the app offers them. The warning face is last and
// is never offered: the cooler switches to it by itself.
QtObject {
    readonly property string warningFace: "limit"

    readonly property var all: [
        { id: "liquid", name: qsTr("Liquid"), component: "LiquidFace.qml", selectable: true },
        { id: "clock", name: qsTr("Clock"), component: "ClockFace.qml", selectable: true },
        { id: "last-minute", name: qsTr("Last minute"), component: "LastMinuteFace.qml", selectable: true },
        { id: "both", name: qsTr("Both"), component: "BothFace.qml", selectable: true },
        { id: "cores", name: qsTr("Cores"), component: "CoresFace.qml", selectable: true },
        { id: "warp", name: qsTr("Warp"), component: "WarpFace.qml", selectable: true },
        { id: "tunnel", name: qsTr("Tunnel"), component: "TunnelFace.qml", selectable: true },
        { id: "reactor", name: qsTr("Reactor"), component: "ReactorFace.qml", selectable: true },
        { id: "hive", name: qsTr("Hive"), component: "HiveFace.qml", selectable: true },
        { id: "matrix", name: qsTr("Matrix"), component: "MatrixFace.qml", selectable: true },
        { id: "neon", name: qsTr("Neon"), component: "NeonFace.qml", selectable: true },
        { id: "rgb-ring", name: qsTr("RGB ring"), component: "RgbRingFace.qml", selectable: true },
        { id: "limit", name: qsTr("At its limit"), component: "LimitFace.qml", selectable: false }
    ]

    readonly property var selectable: all.filter(entry => entry.selectable)

    // "Use my theme colours": what a face is given for the theme's accent.
    // The faces were drawn in Dawn's own colours (the Glacier accent), so
    // with that accent they stay as they are and this is null; any other
    // accent recolours them, with a second colour a sixth of the way round
    // the wheel, as Overclock's green runs to blue on board 10.14. The app's
    // previews and the cooler's own screen both ask here.
    function themeColours(accent, glacier) {
        if (glacier)
            return null
        return {
            accent: accent,
            accentDeep: Qt.hsla((accent.hslHue + 1 / 6) % 1, accent.hslSaturation, accent.hslLightness, 1),
            ink: "#ffffff"
        }
    }

    function find(id) {
        for (const entry of all) {
            if (entry.id === id)
                return entry
        }
        return null
    }

    function source(id) {
        const entry = find(id)
        return entry ? Qt.resolvedUrl(entry.component) : ""
    }
}
