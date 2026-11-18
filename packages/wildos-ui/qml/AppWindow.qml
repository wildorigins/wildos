import QtQuick
import QtQuick.Window

// A Wild OS app window that draws its own title bar: frameless, the glass
// sheet with its 14px corners and 1px edge, the compositor's shadow and
// blur through WindowFrame, and resize grips. Put a TitleBar at the top of
// the content; it moves the window and its buttons act on it.
//
// `deep` picks glass-window-deep (Settings, Welcome, dialogs) over
// glass-window (Files, Store). Without blur, or with transparency effects
// turned off (bind `transparency` to the system setting), the sheet is the
// opaque window colour so text never sits on a raw wallpaper.
//
// The window opens at the size it had when it was last used, maximised if
// it was closed maximised; the width and height set here are what a first
// start gets. An app with several kinds of window gives each a `stateKey`
// ("compose", "preview"): without one only the first window the app makes
// is remembered. `rememberSize: false` turns it off, for a mode that
// resizes the window itself; a window with a fixed width or height never
// remembers.
Window {
    id: window

    property bool deep: false
    property bool transparency: true
    property bool rememberSize: true
    property string stateKey
    readonly property bool maximised: visibility === Window.Maximized || visibility === Window.FullScreen
    readonly property bool glass: frame.blurAvailable && transparency
    readonly property alias frame: frame
    default property alias content: sheet.data

    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    minimumWidth: 640
    minimumHeight: 420

    onActiveChanged: if (active) frame.recheck()

    WindowFrame {
        id: frame
        window: window
        radius: window.maximised ? 0 : Style.windowRadius
        shadowColor: Style.shadowWindow
        blur: window.transparency
    }

    WindowMemory {
        window: window
        enabled: window.rememberSize
        key: window.stateKey
    }

    Rectangle {
        id: sheet
        anchors.fill: parent
        radius: window.maximised ? 0 : Style.windowRadius
        color: !window.glass ? Style.windowOpaque : window.deep ? Style.glassWindowDeep : Style.glassWindow
        border.width: window.maximised ? 0 : 1
        border.color: Style.windowEdge
    }

    ResizeHandles {}
}
