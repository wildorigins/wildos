import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A button over the picture (AppVideos.dc): 34 by 34, radius 8, a white
// line icon (20px for play and pause, 18 for the rest) on a faint white
// hover. The same in both schemes, as the picture is always dark.
T.Button {
    id: control

    property string iconName
    property real iconSize: 18
    property real side: 34
    property bool lit: false
    property string tip
    // Off while the button's menu is open, which the tip would cover.
    property bool tipAllowed: true

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
        color: control.down ? VideosStyle.overPicturePressed : control.hovered && control.enabled ? VideosStyle.overPictureHover : "transparent"

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
            strokeWidth: 1.8
            color: control.lit ? VideosStyle.overPictureAccent : VideosStyle.overPictureIcon
        }
    }

    PictureTip {
        text: control.tip
        visible: control.hovered && control.tipAllowed && control.tip.length > 0 && !control.down
    }
}
