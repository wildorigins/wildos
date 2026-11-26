import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

import "timer.js" as AutoTriggerTimer

// A large glass choice on the power screen: line icon over a one-line label.
// The action the screen was opened for is the primary one and carries the
// accent, the way a chosen card does everywhere else in Wild OS.
T.AbstractButton {
    id: control

    property string iconName
    property bool primary: false

    // Asks the row to move focus to the previous (-1) or next (1) choice.
    signal navigate(int step)

    implicitWidth: Math.max(120, label.implicitWidth + 32)
    implicitHeight: Math.max(120, label.y + label.implicitHeight + 24)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: text

    Keys.onPressed: event => {
        AutoTriggerTimer.cancelAutoTrigger();
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Backtab) {
            control.navigate(-1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Tab) {
            control.navigate(1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            control.clicked();
            event.accepted = true;
        }
    }

    background: Item {
        UI.Glass {
            id: glass
            anchors.fill: parent
            level: "panel"
            shadow: false
            border.color: control.primary ? UI.Style.accentBorderStrong : UI.Style.borderGlass
        }

        // Hover, press and the accent of the primary choice lie over the
        // glass rather than replacing it, so the card never turns see-through.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: glass.radius - 1
            color: control.down ? (control.primary ? UI.Style.accentChip : UI.Style.surfacePressed)
                 : control.primary && control.hovered ? UI.Style.accentBorder
                 : control.primary ? UI.Style.accentSelected
                 : control.hovered ? UI.Style.surfaceHover
                 : UI.Style.surfaceHoverClear

            Behavior on color {
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
        }

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: UI.Style.radiusXl
        }
    }

    contentItem: Item {
        UI.Icon {
            id: icon
            anchors.horizontalCenter: parent.horizontalCenter
            y: 30
            name: control.iconName
            size: 30
            strokeWidth: 1.8
            color: UI.Style.iconInk
        }

        UI.Label {
            id: label
            anchors.horizontalCenter: parent.horizontalCenter
            y: icon.y + icon.height + 14
            text: control.text
            font.weight: UI.Style.weightMedium
        }
    }
}
