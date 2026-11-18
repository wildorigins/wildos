import QtQuick
import WildOS.UI as UI

// The snap layouts popover of Snap.dc: 300px wide, 14px padding, the menu
// glass at radius 14 with its edge and shadow, "Snap this window" over the
// layouts and a muted hint under them. Shared by the hover and drag flyout
// and the Meta+Z overlay, which only change the hint.
UI.Glass {
    id: panel

    property alias grid: grid
    property string hint: ""

    level: "menu"
    width: 300
    height: column.implicitHeight + 28

    // Swallows presses on the panel's own padding so they do not close it.
    MouseArea {
        anchors.fill: parent
    }

    Column {
        id: column
        x: 14
        y: 14
        width: parent.width - 28
        spacing: 10

        UI.Label {
            width: parent.width
            text: qsTr("Snap this window")
            size: 12
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkStatus
        }

        LayoutGrid {
            id: grid
            width: implicitWidth
            height: implicitHeight
        }

        UI.Label {
            visible: panel.hint.length > 0
            width: parent.width
            text: panel.hint
            size: 12
            wrapMode: Text.Wrap
            color: UI.Style.inkMuted
        }
    }
}
