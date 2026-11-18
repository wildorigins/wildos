pragma Singleton

import QtQuick
import WildOS.UI as UI

// The Store's layout and colours from the StoreDiscover to StoreStates
// boards. Colours are WildOS.UI tokens at the boards' strengths, so the
// accent, the text size and light mode reach the Store with the rest of
// the system.
QtObject {
    readonly property int navWidth: 248
    readonly property int topBarHeight: 46
    readonly property int sidePadding: 44
    readonly property int pageTop: 4
    readonly property int sectionGap: 26
    readonly property int columnGap: 34
    readonly property int radiusHero: 16
    readonly property int radiusCard: 14

    // The boards' three strengths of text: ink, 62% and 40%.
    readonly property color inkSecondary: UI.Style.inkAlpha(0.62, 0.68)
    readonly property color inkTertiary: UI.Style.inkAlpha(0.40, 0.52)
    readonly property color inkDim: UI.Style.inkAlpha(0.35, 0.42)
    readonly property color divider: UI.Style.tone(0.08)
    readonly property color sidebar: UI.Style.tone(0.03)
    readonly property color card: UI.Style.tone(0.045)
    readonly property color privacyCard: UI.Style.tone(0.04)
    readonly property color buttonFill: UI.Style.tone(0.10)
    readonly property color buttonHover: UI.Style.tone(0.14)
    readonly property color buttonPressed: UI.Style.tone(0.18)
    readonly property color pill: UI.Style.tone(0.07)
    readonly property color rowHover: UI.Style.tone(0.05)
    readonly property color iconWell: UI.Style.tone(0.07)
    readonly property color ringTrack: UI.Style.tone(0.16)
    readonly property color navSelected: UI.Style.fillSelected(0.16, 0.13)
    readonly property color danger: UI.Style.dark ? "#ff9a8a" : UI.Style.danger
    readonly property color success: UI.Style.success
    readonly property color iconTile: UI.Style.surfaceControl
    readonly property color dialog: UI.Style.glassDialog

    // Hero cards are photographs or, without one, a deep grey card in
    // either scheme, so their words stay white.
    readonly property color heroBase: "#27292d"
    readonly property color onPhoto: UI.Style.inkOnWallpaper
    function onPhotoAlpha(alpha) {
        return Qt.rgba(onPhoto.r, onPhoto.g, onPhoto.b, alpha);
    }
    // Full-size screenshots sit on a deep shade, darker than a dialog's.
    readonly property color viewerShade: Qt.rgba(UI.Style.scrim.r, UI.Style.scrim.g, UI.Style.scrim.b, 0.8)

    // Line icons the boards draw that WildOS.UI has no shape for, on the
    // same 24px grid. Anything else comes from UI.Icons.
    readonly property var icons: ({
        "discover": "M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8zM19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z",
        "swap-windows": "M4 8h14l-3-3M20 16H6l3 3",
        "create": "M18.5 3.5a2.1 2.1 0 0 1 3 3L12 16l-4-4zM8 12c-3 0-4 2-4 4s-1 3-2 4c4 0 8-1 8-4",
        "work": "M3 7h18v13H3zM8 7V4h8v3M3 12h18",
        "play": "M6 8h12a4 4 0 0 1 4 4v2a3 3 0 0 1-5.2 2L15 14H9l-1.8 2A3 3 0 0 1 2 14v-2a4 4 0 0 1 4-4zM7 10.5v3M5.5 12h3M16 11h.01M18 13h.01",
        "develop": "M8 7l-5 5 5 5M16 7l5 5-5 5M14 4l-4 16",
        "categories": "M4 4h6v6H4zM14 4h6v6h-6zM4 14h6v6H4zM14 14h6v6h-6z",
        "windows-apps": "M3 4h18v16H3zM3 9h18M6 6.5h.01M8.5 6.5h.01",
        "updates": "M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7",
        "library": "M12 4v11M7 10l5 5 5-5M5 20h14",
        "private": "M3 3l18 18M10.6 6.1A9.8 9.8 0 0 1 12 6c6 0 9.5 6 9.5 6a17 17 0 0 1-3 3.6M6.3 7.6A16.6 16.6 0 0 0 2.5 12S6 18 12 18a9 9 0 0 0 4-.9M9.9 10a3 3 0 0 0 4.2 4.2",
        "tick": "M5 12l5 5L20 7",
        "reach-file": "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z",
        "reach-image": "M3 5h18v14H3zM3 16l5-5 4 4 3-3 6 6",
        "reach-printer": "M6 9V3h12v6M6 18H4v-7h16v7h-2M6 14h12v7H6z",
        "reach-globe": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM3 12h18M12 3c2.5 2.5 3.5 5.5 3.5 9s-1 6.5-3.5 9c-2.5-2.5-3.5-5.5-3.5-9s1-6.5 3.5-9z",
        "reach-camera": "M3 7h12v10H3zM15 10l6-3v10l-6-3",
        "reach-location": "M12 21s-7-6-7-11a7 7 0 0 1 14 0c0 5-7 11-7 11zM12 12a2 2 0 1 0 0-4 2 2 0 0 0 0 4z",
        "reach-screen": "M3 4h18v12H3zM8 20h8M12 16v4",
        "retry": "M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"
    })

    // What StoreApp calls each thing an app can reach, as icon names.
    readonly property var reachIcons: ({
        "file": "reach-file",
        "folder": "folder",
        "home": "home",
        "drive": "drive",
        "image": "reach-image",
        "music": "music",
        "film": "videos",
        "download": "download",
        "desktop": "reach-screen",
        "gpu": "chip",
        "camera": "reach-camera",
        "mic": "mic",
        "printer": "reach-printer",
        "globe": "reach-globe",
        "location": "reach-location"
    })
}
