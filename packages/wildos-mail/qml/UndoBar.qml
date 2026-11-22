import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// The bar after deleting, archiving or moving (MailMsDelete.dc): what
// happened, Undo, and the Ctrl Z that does the same. It rises from the
// bottom in 160 ms, stays six seconds (longer while the pointer is on it)
// and fades in 120 ms (MailMsMotion.dc). Deleting never asks first.
Rectangle {
    id: bar

    property string text

    function show(message) {
        text = message;
        shown = true;
        hideTimer.restart();
    }

    property bool shown: false

    implicitWidth: Math.min(row.implicitWidth + 32, 560)
    implicitHeight: 48
    radius: UI.Style.radiusMd
    color: UI.Style.glassMenu
    border.width: 1
    border.color: UI.Style.borderMenu
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity {
        NumberAnimation {
            duration: bar.shown ? 160 : 120
            easing.type: Easing.OutCubic
        }
    }
    transform: Translate {
        y: UI.Style.reducedMotion ? 0 : (1 - bar.opacity) * 12
    }
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    HoverHandler {
        onHoveredChanged: hovered ? hideTimer.stop() : hideTimer.restart()
    }

    Timer {
        id: hideTimer
        interval: 6000
        onTriggered: bar.shown = false
    }

    // Nothing to put back any more (undone with Ctrl Z, say): the bar goes.
    Connections {
        target: MailEngine
        function onUndoChanged() {
            if (!MailEngine.canUndo) {
                bar.shown = false;
            }
        }
    }

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        x: 16
        width: parent.width - 32
        spacing: 12

        UI.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 360
            text: bar.text
            elide: Text.ElideRight
        }
        UI.Button {
            size: "small"
            text: qsTr("Undo")
            onClicked: {
                bar.shown = false;
                MailEngine.undo();
            }
        }
        UI.ShortcutKeys {
            keys: ["Ctrl", "Z"]
        }
    }
}
