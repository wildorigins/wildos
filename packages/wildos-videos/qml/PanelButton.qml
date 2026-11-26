import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// An icon button in the side panel and the library (AppVideos.dc): 34 by
// 34, radius 8, a 16px icon; follows the scheme like the panel.
T.Button {
    id: control

    property string iconName
    property real iconSize: 16
    property string tip

    implicitWidth: 34
    implicitHeight: 34
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
        color: control.down ? UI.Style.surfacePressed : control.hovered && control.enabled ? VideosStyle.rowHover : "transparent"

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        VideosIcon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: UI.Style.iconInk
        }
    }

    UI.Tooltip {
        text: control.tip
        visible: control.hovered && control.tip.length > 0 && !control.down
    }
}
