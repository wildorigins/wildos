import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// One reading at the head of the Cooling page (10.10): what it is, with
// its icon in the reading's own colour, the number in 34px weight 300 with
// its unit after it, and a line of context under it.
UI.Card {
    id: tile

    property string iconName
    property color iconColor: UI.Style.accent
    property string name
    property string value
    property string unit
    property string note

    radius: UI.Style.radiusPanel
    clipContent: false
    leftPadding: 17
    rightPadding: 17
    topPadding: 15
    bottomPadding: 15
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1: %2 %3").arg(name).arg(value).arg(unit)
    Accessible.description: note

    contentItem: Column {
        spacing: 6

        Row {
            spacing: 8

            CoolerIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: tile.iconName
                size: 16
                strokeWidth: 2
                color: tile.iconColor
            }

            Words {
                anchors.verticalCenter: parent.verticalCenter
                text: tile.name
                size: 12.5
                color: UI.Style.inkMuted
            }
        }

        Item {
            width: parent.width
            height: 34

            Words {
                id: number
                line: 1
                text: tile.value
                size: 34
                font.weight: UI.Style.weightLight
                font.letterSpacing: -0.02 * UI.Style.px(size)
            }

            Words {
                anchors.left: number.right
                anchors.leftMargin: 5
                anchors.baseline: number.baseline
                text: tile.unit
                size: 15
                color: UI.Style.inkMuted
            }
        }

        Words {
            width: parent.width
            // Keeps its line when there is nothing to say, so the tiles
            // stay one height.
            text: tile.note.length > 0 ? tile.note : " "
            size: 12
            color: UI.Style.inkSubtle
        }
    }
}
