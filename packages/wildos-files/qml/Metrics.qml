pragma Singleton

import QtQuick
import WildOS.UI as UI

// The Files layout from Main.dc.html and FilesDetails.dc.html: the heights
// of its rows and bars, and the few fills only File Explorer's layout has.
// Every colour is built from WildOS.UI tokens, so the accent, the text size
// and light mode reach Files with the rest of the system.
QtObject {
    // A box sized around a line of text keeps the design's height and
    // grows with the text when it is set larger than the default.
    function fit(height, size) {
        return height + Math.max(0, Math.round(size * (UI.Style.textScale - 1) * 1.3));
    }

    readonly property int titleHeight: UI.Style.titleBarHeight
    readonly property int fieldHeight: fit(34, 13)
    readonly property int navHeight: fieldHeight + 17
    readonly property int buttonHeight: fit(34, 13)
    readonly property int commandHeight: buttonHeight + 15
    readonly property int statusHeight: fit(30, 12) + 1
    readonly property int sidebarWidth: 196
    readonly property int rowHeight: fit(38, 13)
    readonly property int placeHeight: fit(32, 13)

    // One fill per region: the title bar is shaded, the navigation row and
    // the current tab share one lighter sheet, the places column is a
    // touch darker than the content.
    readonly property color titleBar: UI.Style.shade(0.30, 0.04)
    readonly property color navBar: UI.Style.tone(0.07)
    readonly property color sidebar: UI.Style.shade(0.20, 0.03)
    readonly property color rule: UI.Style.tone(0.08)
    readonly property color separator: UI.Style.tone(0.12)
    readonly property color hoverSoft: UI.Style.tone(0.05)

    readonly property color bandFill: UI.Style.accentAlpha(0.16, 0.12)
    readonly property color bandBorder: UI.Style.accentAlpha(0.70, 0.55)
    // The trust chip's words sit most of the way to white (Glacier gives
    // #dbe6ff); light mode uses the accent text colour.
    readonly property color chipText: UI.Style.dark
        ? Qt.rgba(UI.Style.accent.r + (1 - UI.Style.accent.r) * 0.7, UI.Style.accent.g + (1 - UI.Style.accent.g) * 0.7, UI.Style.accent.b + (1 - UI.Style.accent.b) * 0.7, 1)
        : UI.Style.accentText
    readonly property color chipHover: UI.Style.fillSelected(0.18, 0.14)
}
