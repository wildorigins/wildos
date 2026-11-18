import QtQuick
import WildOS.Cooler

// A run of lights drawn as a strip (10.11 "Edge lights", 10.15): squares
// `unit` wide, 1.35 units apart, each in its live colour. A strip too long
// for `maxLength` carries on in a second row. `upright` stands it on end,
// for the sections of a split zone side by side (10.11 "Memory").
//
// With no lights and `sockets` above zero it draws that many empty
// outlines: a strip connector nobody has counted yet (10.15 "Zone 3").
Item {
    id: strip

    property int lights: 0
    property var colours: []
    property real unit: 7.2
    property bool upright: false
    property real maxLength: 100000
    property int sockets: 0

    readonly property int shown: lights > 0 ? lights : sockets
    readonly property real pitch: unit * 1.35
    readonly property real edge: unit * 1.25
    readonly property int perRow: Math.max(1, Math.min(shown, Math.floor((maxLength - 2 * edge) / pitch) + 1))
    readonly property int rows: Math.ceil(shown / perRow)
    readonly property real along: shown > 0 ? 2 * edge + (perRow - 1) * pitch : 0
    readonly property real across: shown > 0 ? 2 * edge + (rows - 1) * pitch : 0

    implicitWidth: upright ? across : along
    implicitHeight: upright ? along : across

    Repeater {
        model: strip.lights

        LightDot {
            required property int index
            readonly property real a: strip.edge + (index % strip.perRow) * strip.pitch
            readonly property real b: strip.edge + Math.floor(index / strip.perRow) * strip.pitch
            x: strip.upright ? b : a
            y: strip.upright ? a : b
            size: strip.unit
            colour: strip.colours[index] || "black"
        }
    }

    Repeater {
        model: strip.lights > 0 ? 0 : strip.sockets

        Canvas {
            id: socket
            required property int index
            x: strip.edge + index * strip.pitch - strip.unit / 2 - 1
            y: strip.edge - strip.unit / 2 - 1
            width: strip.unit + 2
            height: strip.unit + 2
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.28);
                ctx.lineWidth = 1;
                ctx.setLineDash([2, 2]);
                ctx.beginPath();
                ctx.roundedRect(1, 1, strip.unit, strip.unit, strip.unit * 0.225, strip.unit * 0.225);
                ctx.stroke();
            }
        }
    }
}
