pragma Singleton

import QtQuick
import WildOS.UI as UI

// Start's own measurements from the Start artboard. Colours and text sizes
// come from WildOS.UI; the four colour names below are the taskbar button's
// (StartButton.qml), passed through to the shared tokens so the button
// follows the accent and the scheme like everything else.
QtObject {
    readonly property color accentTint: UI.Style.accentAlpha(0.26, 0.20)
    readonly property color hover: UI.Style.surfaceHover
    readonly property color pressed: UI.Style.surfacePressed
    readonly property int hoverDuration: UI.Style.hoverDuration

    readonly property int radiusItem: 10
    readonly property int radiusStart: 22
    readonly property int menuWidth: 740
    readonly property int padTop: 26
    readonly property int padSide: 28
    readonly property int sectionGap: 22
    readonly property int footerHeight: 64
    readonly property int searchHeight: 652
}
