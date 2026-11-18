import QtQuick

// A ring of short ticks, the board's dashed circles. Each tick is its own
// small rectangle laid along the ring, so the ring has no seam where a dash
// pattern would run out, and turning it is one rotation.
Item {
    id: ring

    property real radius: 100
    // How far a tick reaches across the ring, and how long it is along it.
    property real thickness: 3
    property real dash: 3
    // From the start of one tick to the start of the next, along the ring.
    property real period: 16
    property int count: Math.ceil(2 * Math.PI * radius / period - 0.01)
    property color colour: "#ffffff"

    width: 640
    height: 640

    Repeater {
        model: ring.count

        Rectangle {
            required property int index
            readonly property real bearing: (index * ring.period + ring.dash / 2) / ring.radius

            x: 320 + ring.radius * Math.cos(bearing) - width / 2
            y: 320 + ring.radius * Math.sin(bearing) - height / 2
            width: ring.thickness
            height: ring.dash
            rotation: bearing * 180 / Math.PI
            color: ring.colour
            antialiasing: true
        }
    }
}
