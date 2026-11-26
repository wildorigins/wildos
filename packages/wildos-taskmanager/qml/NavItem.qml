import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A page in Task Manager's list (TaskManager.dc): 38px, radius 8, a 16px
// line icon at 12, the name in 13px after it; the current page sits on the
// hover tint in weight 600.
T.AbstractButton {
    id: item

    property string iconName
    property bool current: false

    implicitHeight: Math.max(38, label.implicitHeight + 16)
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: current

    background: Rectangle {
        radius: UI.Style.radiusSm
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: item.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: item.down ? UI.Style.surfacePressed : item.current ? UI.Style.surfaceHover : item.hovered ? UI.Style.surfaceControl : "transparent"

        UI.FocusRing {
            visible: item.visualFocus
            targetRadius: UI.Style.radiusSm
        }
    }

    contentItem: Item {
        UI.Icon {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            name: item.iconName
            size: 16
            strokeWidth: 1.8
        }

        UI.Label {
            id: label
            x: 40
            width: parent.width - x - 12
            anchors.verticalCenter: parent.verticalCenter
            text: item.text
            font.weight: item.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
        }
    }
}
