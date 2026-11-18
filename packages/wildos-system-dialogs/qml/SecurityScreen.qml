import QtQuick
import QtQuick.Window
import WildOS.UI as UI
import WildOS.SystemDialogs
import WildOS.SystemDialogs.Backend

// The Ctrl+Alt+Del screen on the desktop. The session service keeps it
// loaded and hidden, so it appears the moment KWin sees the keys, and it
// never runs inside the desktop shell, so a frozen shell or app cannot
// stop it. A layer surface over every screen: the desktop blurred and at
// 55% brightness (CtrlAltDel.dc blur(30px) brightness(0.55)).
Window {
    id: window

    visible: false
    color: "transparent"
    title: qsTr("Security options")

    Surface {
        id: surface
        window: window
        role: "overlay"
        scope: "wildos-security"
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(UI.Style.wallpaperDim.r, UI.Style.wallpaperDim.g, UI.Style.wallpaperDim.b, surface.blurAvailable ? 0.5 : 0.85)
    }

    MouseArea {
        anchors.fill: parent
    }

    SecurityContent {
        id: content
        anchors.fill: parent
        focus: true
    }

    Instantiator {
        model: Qt.application.screens
        delegate: Backdrop {
            required property var modelData
            required property int index
            dim: 0.5
            // From the list itself (see PermissionPrompt.qml).
            targetScreen: Qt.application.screens[index]
            open: Security.open && modelData.name !== window.screen.name
        }
    }

    function sync() {
        if (Security.open) {
            surface.show();
            content.takeFocus();
        } else {
            surface.hide();
        }
    }

    Connections {
        target: Security
        function onOpenChanged() {
            window.sync();
        }
    }

    Component.onCompleted: sync()
}
