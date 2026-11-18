pragma Singleton

import QtQuick
import WildOS.UI as UI

// The colours the Cooler boards give each reading (10.10: the processor in
// the accent, the liquid sky blue, the pump violet, the fans rose) and the
// green of "Connected and working". Light mode deepens each hue so a thin
// line still reads on pale glass. Everything else comes from UI.Style.
QtObject {
    readonly property bool dark: UI.Style.dark

    readonly property color cpu: UI.Style.accent
    readonly property color liquid: dark ? "#7fd4ff" : "#0b7fb8"
    readonly property color pump: dark ? "#b79bff" : "#6a4fcf"
    readonly property color fans: dark ? "#f0a3ae" : "#c0455a"
    readonly property color good: dark ? "#5fd38d" : "#1d7a4c"

    // The cooler's body around its screen, the same in either scheme: it is
    // a picture of the part, not a surface of the window.
    readonly property color bezelLight: "#24252b"
    readonly property color bezelDark: "#0c0d10"
    readonly property color bezelEdge: Qt.rgba(1, 1, 1, 0.14)
    readonly property color bezelShadow: Qt.rgba(0, 0, 0, 0.55)
    readonly property color screenOff: "#000000"
    readonly property color screenEdge: Qt.rgba(1, 1, 1, 0.06)

    // The nav column is a shade darker than the window (10.9: black at
    // 0.12), like a tinted title bar.
    readonly property color navShade: UI.Style.contrast ? "transparent" : dark ? Qt.rgba(0, 0, 0, 0.12) : Qt.rgba(0, 0, 0, 0.03)

    function alpha(colour, a) {
        return Qt.rgba(colour.r, colour.g, colour.b, a);
    }
}
