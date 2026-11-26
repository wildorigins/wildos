import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A marker of a finished recording, under the buttons: its flag and the
// time, which a click jumps to.
T.Button {
    id: control

    property double time

    implicitWidth: row.implicitWidth + 22
    implicitHeight: 28
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: text

    background: Rectangle {
        radius: height / 2
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: control.down ? UI.Style.surfacePressed : control.hovered ? UI.Style.surfaceHover : UI.Style.surfaceControl
        border.width: 1
        border.color: UI.Style.borderSubtle

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6

            VoiceIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "flag"
                size: 13
                color: UI.Style.accent
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: VoiceStyle.time(control.time)
                size: 12
                color: UI.Style.inkMuted
                font.features: { "tnum": 1 }
            }
        }
    }
}
