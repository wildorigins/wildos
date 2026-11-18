pragma Singleton

import QtQuick
import WildOS.UI as UI

// The board's colours that WildOS.UI has no token for: the log's source
// names, and the plain fields and panes of this window. The green of
// "nothing is sent" and of removed details is the system's success green,
// with the board's lighter washes of it. Light values are the dark ones
// carried over the way Style carries its own.
QtObject {
    readonly property bool dark: UI.Style.dark
    readonly property color green: UI.Style.success
    readonly property color greenFill: Qt.rgba(green.r, green.g, green.b, 0.07)
    readonly property color greenEdge: Qt.rgba(green.r, green.g, green.b, dark ? 0.2 : 0.25)
    readonly property color tokenFill: UI.Style.successSoft
    readonly property color source: dark ? "#c9a6ff" : "#6a3fc2"
    readonly property color field: UI.Style.tone(0.05)
    readonly property color fieldEdge: UI.Style.tone(0.10)
    readonly property color hairline: UI.Style.tone(0.07)
    readonly property color nav: UI.Style.contrast ? "#000000" : dark ? Qt.rgba(0, 0, 0, 0.12) : Qt.rgba(0, 0, 0, 0.03)
    readonly property color navEdge: UI.Style.tone(0.08)
    readonly property color pane: UI.Style.contrast ? "#000000" : dark ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(0, 0, 0, 0.035)
    readonly property color footer: UI.Style.tone(0.02)
    readonly property color chip: UI.Style.tone(0.06)
    readonly property color selected: UI.Style.tone(0.12)
    readonly property string mono: "JetBrains Mono"
}
