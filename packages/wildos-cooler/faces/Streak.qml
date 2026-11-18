import QtQuick

// A straight stroke with round ends, from one point to another. Drawn as a
// rounded rectangle so that hundreds of them batch into a few draw calls and
// cost nothing while only their parent moves.
Rectangle {
    id: streak

    property real x1: 0
    property real y1: 0
    property real x2: 0
    property real y2: 0
    property real thickness: 2

    x: x1 - thickness / 2
    y: y1 - thickness / 2
    width: Math.hypot(x2 - x1, y2 - y1) + thickness
    height: thickness
    radius: thickness / 2
    antialiasing: true
    transform: Rotation {
        origin.x: streak.thickness / 2
        origin.y: streak.thickness / 2
        angle: Math.atan2(streak.y2 - streak.y1, streak.x2 - streak.x1) * 180 / Math.PI
    }
}
