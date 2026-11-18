import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// "Flash" (10.15): a 30px button with the lightning mark. While its zone
// is blinking it turns to the accent and reads "Flashing".
T.AbstractButton {
    id: button

    property bool flashing: false
    property string idleText: qsTr("Flash")

    text: flashing ? qsTr("Flashing") : idleText
    implicitWidth: 1 + 12 + 13 + 7 + label.implicitWidth + 12 + 1
    implicitHeight: 32
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    background: Rectangle {
        radius: 7
        color: button.flashing ? UI.Style.accentAlpha(0.16, 0.12) : button.down ? UI.Style.surfacePressed : button.hovered ? UI.Style.surfaceHover : UI.Style.tone(0.05)
        border.width: 1
        border.color: button.flashing ? UI.Style.accent : UI.Style.borderSubtle
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: button.visualFocus
            targetRadius: 7
        }
    }

    contentItem: Item {
        LightIcon {
            x: 13
            y: Math.round((parent.height - height) / 2)
            name: "flash"
            size: 13
            strokeWidth: 2
            color: button.flashing ? UI.Style.accent : UI.Style.inkMuted
        }

        Words {
            id: label
            x: 13 + 13 + 7
            y: Math.round((parent.height - height) / 2)
            text: button.text
            size: 12.5
            font.weight: UI.Style.weightSemiBold
            color: button.flashing ? UI.Style.accent : UI.Style.ink
        }
    }
}
