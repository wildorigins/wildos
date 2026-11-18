import QtQuick
import QtQuick.Window
import WildOS.UI as UI
import org.wildos.overlays

// Caps Lock and Num Lock (Overlays.dc): a 160 by 120 square in the middle of
// the screen, radius 20, the panel glass, a 32px glyph over "Caps Lock on".
// It shows when either key changes, from any keyboard, and fades 1.5
// seconds later; clicks pass through it. It lives with the clock because
// the clock is the taskbar's home for system pop-ups.
Item {
    id: lockKeys

    required property var screenInfo

    property bool caps: true
    property bool on: true

    KeyState {
        id: keys
        onToggled: (capsLock, on) => {
            lockKeys.caps = capsLock;
            lockKeys.on = on;
            fade.stop();
            card.opacity = 1;
            frame.blur = true;
            square.visible = true;
            hold.restart();
        }
    }

    Timer {
        id: hold
        interval: 1500
        onTriggered: {
            if (UI.Style.reducedMotion) {
                square.visible = false;
            } else {
                // The compositor's blur cannot fade, so it goes first.
                frame.blur = false;
                fade.restart();
            }
        }
    }

    Window {
        id: square

        screen: lockKeys.screenInfo
        width: 160
        height: 120
        color: "transparent"
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
            | (placement.wayland ? 0 : Qt.ToolTip)
        title: label.text
        visible: false

        OverlayPlacement {
            id: placement
            window: square
            layer: OverlayPlacement.Overlay
            keyboard: OverlayPlacement.NoKeyboard
            scope: "wildos-osd"
        }

        UI.WindowFrame {
            id: frame
            window: square
            radius: 20
            shadowColor: UI.Style.shadowWindow
        }

        Rectangle {
            id: card
            anchors.fill: parent
            radius: 20
            color: UI.Style.glassFloat
            border.width: 1
            border.color: UI.Style.borderGlass

            Column {
                anchors.centerIn: parent
                spacing: 10

                UI.Glyph {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 32
                    height: 32
                    strokeWidth: 1.9
                    // Caps Lock's arrow over a bar; Num Lock's key with a 1.
                    path: lockKeys.caps ? "M12 4l7 8h-4v4H9v-4H5zM9 20h6"
                        : "M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM10.5 9.5l2-1.5v8"
                    color: lockKeys.on ? UI.Style.iconInk : UI.Style.iconMuted
                }

                UI.Label {
                    id: label
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 13
                    text: lockKeys.caps ? (lockKeys.on ? qsTr("Caps Lock on") : qsTr("Caps Lock off"))
                        : (lockKeys.on ? qsTr("Num Lock on") : qsTr("Num Lock off"))
                }
            }
        }

        // The window cannot fade itself on Wayland, so the card does, and
        // the window goes once it is clear.
        NumberAnimation {
            id: fade
            target: card
            property: "opacity"
            to: 0
            duration: 200
            onFinished: square.visible = false
        }
    }
}
