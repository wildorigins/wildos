import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// A page in Cooler's list (10.9): 34px, radius 6, 8px in from the column's
// edges, a 16px line icon and the name in 13px. The current page sits on
// the selected fill in weight 550 with the accent bar at its left; with no
// cooler (10.13) every page is there but faded and cannot be opened.
T.AbstractButton {
    id: item

    property string iconName
    property bool current: false

    x: 8
    width: parent ? parent.width - 16 : implicitWidth
    implicitHeight: Math.max(34, label.implicitHeight + 14)
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: current

    background: Rectangle {
        radius: UI.Style.radiusXs
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: item.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: item.down ? UI.Style.surfacePressed : item.current ? UI.Style.surfaceSelected : item.hovered ? UI.Style.surfaceControl : "transparent"

        Rectangle {
            visible: item.current
            y: 9
            width: 3
            height: parent.height - 18
            radius: 2
            color: UI.Style.accent
        }

        UI.FocusRing {
            visible: item.visualFocus
            targetRadius: UI.Style.radiusXs
        }
    }

    contentItem: Item {
        CoolerIcon {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            name: item.iconName
            size: 16
            strokeWidth: 2
            color: item.current ? UI.Style.ink : UI.Style.inkMuted
        }

        Words {
            id: label
            x: 37
            width: parent.width - x - 10
            anchors.verticalCenter: parent.verticalCenter
            text: item.text
            font.weight: item.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
            opacity: item.enabled ? 1 : 0.45
        }
    }
}
