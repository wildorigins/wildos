import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// An icon button from the board (AppNotes.dc): 30 by 30 on the formatting
// bar, 34 by 34 beside the list heading and on the right of the bar, radius
// 8. `lit` draws the icon in the accent, as the pin of a pinned note is.
T.Button {
    id: control

    property string iconName
    property real iconSize: 16
    property real side: 34
    property bool lit: false
    property bool active: false
    property string tip

    implicitWidth: side
    implicitHeight: side
    padding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
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
        color: control.down ? UI.Style.surfacePressed : control.active ? UI.Style.surfaceSelected : control.hovered && control.enabled ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        NotesIcon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.lit ? UI.Style.accent : UI.Style.iconInk
        }
    }

    UI.Tooltip {
        text: control.tip
        visible: control.hovered && control.tip.length > 0 && !control.down
    }
}
