import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A column heading in the Processes table (TaskManager.dc): the column's
// total in 15px weight 600 over its name in 11px, right aligned for
// numbers. Clicking sorts by the column; the sorted one shows a small
// chevron, as Windows does.
T.AbstractButton {
    id: heading

    property string total
    property string label
    property bool alignRight: true
    property bool sorted: false
    property bool descending: true

    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.ColumnHeader
    Accessible.name: label + (total.length > 0 ? ", " + total : "")

    background: Rectangle {
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: heading.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: heading.down ? UI.Style.surfacePressed : heading.hovered ? UI.Style.surfaceRaised : Qt.alpha(UI.Style.surfaceRaised, 0)

        UI.FocusRing {
            visible: heading.visualFocus
            anchors.margins: 2
            targetRadius: 4
        }
    }

    contentItem: Item {
        UI.Icon {
            visible: heading.sorted
            anchors.horizontalCenter: parent.horizontalCenter
            y: 3
            name: heading.descending ? "chevron-down" : "chevron-up"
            size: 10
            strokeWidth: 2
            color: UI.Style.iconMuted
        }

        Column {
            x: 12
            width: parent.width - 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            UI.Label {
                width: parent.width
                height: Math.max(implicitHeight, UI.Style.px(15) + 3)
                text: heading.total
                size: 15
                font.weight: UI.Style.weightSemiBold
                font.features: { "tnum": 1 }
                horizontalAlignment: heading.alignRight ? Text.AlignRight : Text.AlignLeft
            }

            UI.Label {
                width: parent.width
                text: heading.label
                size: 11
                color: UI.Style.inkAlpha(0.7, 0.64)
                horizontalAlignment: heading.alignRight ? Text.AlignRight : Text.AlignLeft
            }
        }
    }
}
