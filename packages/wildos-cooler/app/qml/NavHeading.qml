import QtQuick
import WildOS.UI as UI

// A heading over a group of pages in the list (10.9 "NZXT Kraken", "This
// PC"): 12px weight 550 in the subtle ink, 14px in from the edge.
Item {
    property alias text: label.text

    width: parent ? parent.width : implicitWidth
    implicitHeight: 14 + label.implicitHeight + 6

    Words {
        id: label
        x: 14
        y: 14
        width: parent.width - 28
        size: 12
        font.weight: UI.Style.weightSemiBold
        color: UI.Style.inkSubtle
    }
}
