import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// One face in the grid (10.9): the cooler at 92px with the face on it and
// its name under it. The chosen one sits on the selected fill inside a 2px
// accent edge with a tick at its corner. The face stands still until the
// pointer is over it, so a grid of twelve costs nothing to show.
T.AbstractButton {
    id: tile

    property string faceId
    property bool current: false
    // The page's clock, followed only while hovered.
    property real clock: 0
    readonly property real stillTime: 1.5

    implicitWidth: 100
    implicitHeight: 10 + 92 + 8 + label.implicitHeight + 9
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.RadioButton
    Accessible.name: text
    Accessible.checked: current

    background: Rectangle {
        radius: UI.Style.radiusMd
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: tile.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: tile.current ? UI.Style.surfaceSelected : tile.down ? UI.Style.surfacePressed : tile.hovered ? UI.Style.surfaceControl : "transparent"
        border.width: tile.current ? 2 : 0
        border.color: UI.Style.accent

        UI.FocusRing {
            visible: tile.visualFocus
            targetRadius: UI.Style.radiusMd
        }
    }

    contentItem: Item {
        Bezel {
            y: 10
            anchors.horizontalCenter: parent.horizontalCenter
            size: 92
            faceId: tile.faceId
            time: tile.hovered ? tile.clock : tile.stillTime
        }

        Words {
            id: label
            y: 110
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: tile.text
            font.weight: tile.current ? UI.Style.weightBold : 450
        }

        Rectangle {
            visible: tile.current
            x: parent.width - width - 7
            y: 7
            width: 18
            height: 18
            radius: 9
            color: UI.Style.accent

            CoolerIcon {
                anchors.centerIn: parent
                name: "check"
                size: 13
                strokeWidth: 3
                color: UI.Style.textOnAccent
            }
        }
    }
}
