import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// The readings at the foot of the page list (10.9): the liquid's
// temperature over the pump and fan speeds, whichever page is open. Fans
// that are not plugged into the cooler have no speed to show.
UI.Card {
    id: strip

    radius: UI.Style.radiusPanel
    clipContent: false
    leftPadding: 13
    rightPadding: 13
    topPadding: 12
    bottomPadding: 12

    contentItem: Item {
        implicitHeight: Math.max(18, text.implicitHeight)

        CoolerIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: "drop"
            size: 18
            color: Colours.liquid
        }

        Column {
            id: text
            x: 28
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Words {
                width: parent.width
                text: qsTr("Liquid %1").arg(Format.degrees(CoolerApp.liquidTemperature))
                size: 12
                font.weight: UI.Style.weightSemiBold
            }

            Words {
                width: parent.width
                text: CoolerApp.fansConnected
                    ? qsTr("Pump %1 · Fans %2").arg(Format.count(CoolerApp.pumpRpm)).arg(Format.count(CoolerApp.fanRpm))
                    : qsTr("Pump %1").arg(Format.count(CoolerApp.pumpRpm))
                size: 11
                color: UI.Style.inkMuted
            }
        }
    }
}
