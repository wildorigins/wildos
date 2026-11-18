import QtQuick
import QtQuick.Window
import WildOS.UI as UI
import WildOS.SystemDialogs
import WildOS.SystemDialogs.Backend

// The permission prompt on the desktop: every screen dims and blurs
// (DialogPermission.dc rgba(12,14,18,0.55) with an 8px blur) under a layer
// surface above every window, which holds the keyboard until Yes or No.
// Apps cannot draw over it or put up a look-alike, because only the
// shell may make surfaces like this one.
Window {
    id: window

    visible: false
    color: "transparent"
    title: qsTr("Allow changes")

    Surface {
        id: surface
        window: window
        role: "overlay"
        scope: "wildos-permission"
    }

    Rectangle {
        id: dim
        anchors.fill: parent
        color: Qt.rgba(UI.Style.scrim.r, UI.Style.scrim.g, UI.Style.scrim.b, surface.blurAvailable ? 0.55 : 0.8)
    }

    // Clicks on the dim do nothing, as on Windows.
    MouseArea {
        anchors.fill: parent
    }

    PermissionCard {
        id: card
        x: Math.round((window.width - width) / 2)
        // Where the board puts it (190 of 900); it grows downwards when the
        // details open rather than jumping.
        y: Math.max(24, Math.min(Math.round(window.height * 0.21), window.height - height - 24))
        height: implicitHeight
    }

    // The dim and the card arrive as every Wild OS dialog does. The password
    // box has the keyboard from the first frame, so typing never waits.
    ParallelAnimation {
        id: arrive
        NumberAnimation { target: dim; property: "opacity"; from: 0; to: 1; duration: UI.Style.motionFast; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: UI.Style.motionFast; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "scale"; from: UI.Style.scaleEnter; to: 1; duration: UI.Style.motionNormal; easing.type: UI.Style.easeEnter }
    }

    Instantiator {
        model: Qt.application.screens
        delegate: Backdrop {
            required property var modelData
            required property int index
            // From the list itself: the model's copy of a screen is not
            // taken as one.
            targetScreen: Qt.application.screens[index]
            open: Agent.active && modelData.name !== window.screen.name
        }
    }

    Connections {
        target: Agent
        function onActiveChanged() {
            if (Agent.active) {
                surface.show();
                card.takeFocus();
                arrive.restart();
            } else {
                surface.hide();
            }
        }
    }

    Component.onCompleted: {
        if (Agent.active) {
            surface.show();
            card.takeFocus();
            arrive.restart();
        }
    }
}
