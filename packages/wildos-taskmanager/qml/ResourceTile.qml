import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A resource in the Performance sidebar (TaskManagerPerf.dc): 64px, radius
// 10, its 70 by 32 graph edged in its colour, the name in 13px weight 600
// and a one-line summary in 11px under it. The open one sits on 0.08.
T.AbstractButton {
    id: tile

    property string title
    property string subtitle
    property var values: []
    property real maximum: 100
    property color colour: UI.Style.accent
    property bool current: false

    implicitHeight: 64
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: title + ", " + subtitle
    Accessible.selected: current

    background: Rectangle {
        radius: 10
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: tile.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: tile.down ? UI.Style.surfacePressed : tile.current ? UI.Style.tone(0.08) : tile.hovered ? UI.Style.surfaceRaised : Qt.alpha(UI.Style.surfaceRaised, 0)

        UI.FocusRing {
            visible: tile.visualFocus
            targetRadius: 10
        }
    }

    contentItem: Item {
        Graph {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 70
            height: 32
            values: tile.values
            maximum: tile.maximum
            colour: tile.colour
            lineWidth: 1.4
            fillOpacity: 0.25
            edgeOpacity: 1
            radius: 4
        }

        Column {
            x: 12 + 70 + 12
            width: parent.width - x - 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            UI.Label {
                width: parent.width
                text: tile.title
                font.weight: UI.Style.weightSemiBold
            }

            UI.Label {
                width: parent.width
                text: tile.subtitle
                size: 11
                color: UI.Style.inkMuted
            }
        }
    }
}
