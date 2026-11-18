import QtQuick
import WildOS.Cooler

// A run of lights drawn as a ring (10.11 "Pump ring" and the fans): the
// first light at the top, the rest clockwise. A small ring is drawn round
// a hub, as a fan's lights sit round its motor; a ring of many lights is
// left open.
Item {
    id: ring

    property int lights: 0
    property var colours: []
    property real size: 58
    readonly property bool hub: lights <= 16

    readonly property real orbit: size * 0.37
    readonly property real dot: lights > 0 ? Math.min(size * 0.062, 0.307 * 2 * Math.PI * orbit / lights) : 0

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        visible: ring.hub
        anchors.centerIn: parent
        width: ring.size * 0.54
        height: width
        radius: width / 2
        color: LightWords.hub
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.07)
    }

    Rectangle {
        visible: ring.hub
        anchors.centerIn: parent
        width: ring.size * 0.12
        height: width
        radius: width / 2
        color: LightWords.unlit
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
    }

    Repeater {
        model: ring.lights

        LightDot {
            required property int index
            readonly property real angle: 2 * Math.PI * index / ring.lights - Math.PI / 2
            x: ring.size / 2 + ring.orbit * Math.cos(angle)
            y: ring.size / 2 + ring.orbit * Math.sin(angle)
            size: ring.dot * 2
            glow: ring.dot * 2.11
            round: true
            colour: ring.colours[index] || "black"
        }
    }
}
