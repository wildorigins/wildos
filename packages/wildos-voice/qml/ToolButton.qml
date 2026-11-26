import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// An icon button from the board (AppVoice.dc): 34 by 34, radius 8, a 16px
// icon, as Search beside "Recordings" is.
T.Button {
    id: control

    property string iconName
    property real iconSize: 16
    property real side: 34
    property color iconColor: UI.Style.iconInk
    property string tip

    implicitWidth: side
    implicitHeight: side
    padding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    Accessible.name: tip
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: UI.Style.radiusSm
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: control.down ? UI.Style.surfacePressed : control.hovered && control.enabled ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        VoiceIcon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.iconColor
        }
    }

    UI.Tooltip {
        text: control.tip
        visible: control.hovered && control.tip.length > 0 && !control.down
    }
}
