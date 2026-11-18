pragma Singleton

import QtQuick

// SVG path data for the flyout's line icons, copied from the design on a 24 px
// grid. Circles are written as two arcs because Shapes has no circle element.
QtObject {
    readonly property string wifi: "M2.5 9a14 14 0 0 1 19 0M5.5 12.5a9.5 9.5 0 0 1 13 0M8.6 16a5 5 0 0 1 6.8 0"
    readonly property string wifiArcOuter: "M2.5 9a14 14 0 0 1 19 0"
    readonly property string wifiArcMiddle: "M5.5 12.5a9.5 9.5 0 0 1 13 0"
    readonly property string wifiArcInner: "M8.6 16a5 5 0 0 1 6.8 0"
    readonly property string wifiDot: "M13.6 19.5a1.6 1.6 0 1 1-3.2 0a1.6 1.6 0 1 1 3.2 0z"
    readonly property string wifiDotTray: "M13.5 19.5a1.5 1.5 0 1 1-3 0a1.5 1.5 0 1 1 3 0z"
    readonly property string slash: "M3.5 3.5l17 17"
    // Windows 11's Ethernet glyph: a screen with a network plug hanging
    // from its lower left corner, so it does not read as a plain monitor.
    readonly property string ethernet: "M6 3.5h12.5a1.5 1.5 0 0 1 1.5 1.5v8.5a1.5 1.5 0 0 1-1.5 1.5H6a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 6 3.5zM8 15v2.5M5.5 17.5h5V21h-5z"
    // The Ethernet port drawn on the desktop quick settings (EmptyStates.dc).
    readonly property string ethernetPort: "M6 4h12a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM9 16v4h6v-4M8 8v3M12 8v3M16 8v3"
    readonly property string globe: "M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0zM3 12h18M12 3c2.4 2.5 3.6 5.5 3.6 9s-1.2 6.5-3.6 9c-2.4-2.5-3.6-5.5-3.6-9S9.6 5.5 12 3z"
    readonly property string computer: "M4 5h16v11H4zM9 20h6M12 16v4"
    readonly property string bluetooth: "M7 7l10 10-5 5V2l5 5L7 17"
    readonly property string flight: "M12 3v6M8.5 9h7M12 9l-9 6v2l9-3 9 3v-2zM12 14v5M9 21h6"
    readonly property string moon: "M20 14.5A8.5 8.5 0 0 1 9.5 4 8.5 8.5 0 1 0 20 14.5z"
    readonly property string coffee: "M5 9.5h11V14a5 5 0 0 1-5 5h-1a5 5 0 0 1-5-5V9.5zM16 11h1.5a2.5 2.5 0 0 1 0 5H16M8 3.5v3M10.5 3.5v3M13 3.5v3"
    readonly property string sun: "M16 12a4 4 0 1 1-8 0a4 4 0 1 1 8 0zM12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"
    readonly property string vpn: "M12 3l7 3v6c0 4.4-3 7.6-7 9-4-1.4-7-4.6-7-9V6zM9.5 12h5"
    readonly property string speaker: "M4 9.5h4l5-4v13l-5-4H4z"
    readonly property string waveNear: "M16.5 9a4.5 4.5 0 0 1 0 6"
    readonly property string waveFar: "M19 6.5a8 8 0 0 1 0 11"
    readonly property string muteCross: "M16.5 9.5l5 5M21.5 9.5l-5 5"
    readonly property string pencil: "M4 20h4L19 9l-4-4L4 16z"
    readonly property string check: "M5 12.5l4.5 4.5L19 7.5"
    readonly property string cog: "M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2zM15 12a3 3 0 1 1-6 0a3 3 0 1 1 6 0z"
    readonly property string chevronLeft: "M15 6l-6 6 6 6"
    readonly property string chevronRight: "M9 6l6 6-6 6"
    readonly property string plus: "M12 5v14M5 12h14"
    readonly property string minus: "M5 12h14"
    readonly property string headphones: "M4 15v-3a8 8 0 0 1 16 0v3M4 15h3v5H4zM17 15h3v5h-3z"
    readonly property string keyboard: "M3 7h18v10H3zM6.5 10.5h1M10.5 10.5h1M14.5 10.5h1M8 14h8"
    readonly property string mouse: "M12 3a6 6 0 0 1 6 6v6a6 6 0 0 1-12 0V9a6 6 0 0 1 6-6zM12 7v3"
    // Cast and Accessibility, as EmptyStates.dc draws them.
    readonly property string cast: "M3 17a4 4 0 0 1 4 4M3 13a8 8 0 0 1 8 8M3 9v-2a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2h-4"
    readonly property string accessibility: "M13.8 4.5a1.8 1.8 0 1 1-3.6 0a1.8 1.8 0 1 1 3.6 0zM5 8.5l7 1.5 7-1.5M12 10v5M9 21l3-6 3 6"
    readonly property string bolt: "M13 3L5 14h6l-1 7 8-11h-6z"
    readonly property string device: "M7 3h10v18H7zM11 18h2"
    readonly property string play: "M8 5.5v13l10.5-6.5z"
    readonly property string pause: "M9 6v12M15 6v12"
    readonly property string skipNext: "M6 6l9 6-9 6zM18 6v12"
    readonly property string skipPrevious: "M18 6l-9 6 9 6zM6 6v12"
}
