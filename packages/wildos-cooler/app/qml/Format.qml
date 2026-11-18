pragma Singleton

import QtQuick

// How Cooler writes numbers and names the faces.
QtObject {
    // 2100 as "2,100", in the display language's own grouping.
    function count(value) {
        return Number(Math.round(value)).toLocaleString(Qt.locale(), "f", 0);
    }

    // A temperature that is not known (below nought) is written as nothing.
    function degrees(value) {
        return value >= 0 ? qsTr("%1°").arg(Math.round(value)) : "";
    }

    // The twelve faces a person can choose, in the board's order. The
    // thirteenth, the red warning ("limit"), is shown by the cooler itself.
    readonly property var faces: [
        { id: "liquid", name: qsTr("Liquid") },
        { id: "clock", name: qsTr("Clock") },
        { id: "last-minute", name: qsTr("Last minute") },
        { id: "both", name: qsTr("Both") },
        { id: "cores", name: qsTr("Cores") },
        { id: "warp", name: qsTr("Warp") },
        { id: "tunnel", name: qsTr("Tunnel") },
        { id: "reactor", name: qsTr("Reactor") },
        { id: "hive", name: qsTr("Hive") },
        { id: "matrix", name: qsTr("Matrix") },
        { id: "neon", name: qsTr("Neon") },
        { id: "rgb-ring", name: qsTr("RGB ring") }
    ]

    function faceName(id) {
        if (id === "limit") {
            return qsTr("At its limit");
        }
        const face = faces.find(f => f.id === id);
        return face ? face.name : "";
    }
}
