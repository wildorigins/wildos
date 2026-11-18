import QtQuick
import WildOS.Cooler

// One light of a strip or a ring: its colour with a soft glow of the same
// colour around it (10.11). A light that is off is a dark socket with a
// faint edge, so it can still be counted.
Item {
    id: dot

    // The light's centre is the item's own origin.
    property color colour: "black"
    property real size: 7.2
    property real glow: size * 1.055
    property bool round: false

    readonly property bool lit: colour.r + colour.g + colour.b > 0.06

    Rectangle {
        visible: dot.lit
        x: -dot.glow
        y: -dot.glow
        width: dot.glow * 2
        height: dot.glow * 2
        radius: dot.glow
        color: Qt.rgba(dot.colour.r, dot.colour.g, dot.colour.b, 0.22)
    }

    Rectangle {
        x: -dot.size / 2
        y: -dot.size / 2
        width: dot.size
        height: dot.size
        radius: dot.round ? dot.size / 2 : dot.size * 0.225
        color: dot.lit ? dot.colour : LightWords.unlit
        border.width: dot.lit ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
    }
}
