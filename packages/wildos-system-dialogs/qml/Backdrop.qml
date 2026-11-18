import QtQuick
import QtQuick.Window
import WildOS.UI as UI
import WildOS.SystemDialogs

// The dimmed, blurred cover on a screen other than the one with the
// dialog, so nothing on any screen can be clicked or read past it.
Window {
    id: window

    property bool open: false
    property real dim: 0.55
    // The screen this cover belongs on. It is given to the window right
    // before it shows, never as a binding: Qt puts a window made inside
    // another back on that window's screen while it is being made, and a
    // cover shown then went to the first screen, over the dialog.
    property var targetScreen: null
    property bool ready: false

    visible: false
    color: "transparent"

    Surface {
        id: surface
        window: window
        role: "backdrop"
        scope: "wildos-backdrop"
    }

    Rectangle {
        id: cover
        anchors.fill: parent
        color: Qt.rgba(UI.Style.wallpaperDim.r, UI.Style.wallpaperDim.g, UI.Style.wallpaperDim.b, surface.blurAvailable ? window.dim : Math.min(0.9, window.dim + 0.3))
    }

    NumberAnimation {
        id: arrive
        target: cover
        property: "opacity"
        from: 0
        to: 1
        duration: UI.Style.motionFast
        easing.type: Easing.OutCubic
    }

    function apply() {
        if (open) {
            if (targetScreen) {
                window.screen = targetScreen;
            }
            surface.show();
            arrive.restart();
        } else {
            surface.hide();
        }
    }

    // A dialog that starts already open (Ctrl+Alt+Del) sets open while the
    // cover is still being made; it shows once it is complete.
    onOpenChanged: {
        if (ready) {
            apply();
        }
    }
    Component.onCompleted: {
        ready = true;
        if (open) {
            apply();
        }
    }
}
