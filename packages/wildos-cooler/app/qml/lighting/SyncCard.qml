import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// Whether a part follows the effect chosen for the whole PC or has one of
// its own, and the button that changes it (10.15, 10.16).
UI.Card {
    id: card

    property bool own: false

    signal toggled(bool own)

    radius: UI.Style.radiusPanel
    clipContent: false
    padding: 1

    contentItem: Item {
        implicitHeight: Math.max(50, words.height + 16)

        LightIcon {
            x: 16
            y: Math.round((parent.height - height) / 2)
            name: "link"
        }

        Column {
            id: words
            x: 46
            width: Math.max(0, button.x - 10 - x)
            y: Math.round((parent.height - height) / 2)
            spacing: 1

            Words {
                width: parent.width
                text: card.own ? qsTr("Set on its own") : qsTr("Following everything")
                size: 13
                wrapMode: Text.Wrap
            }

            Words {
                width: parent.width
                text: card.own ? qsTr("This part has its own effect, apart from the rest") : qsTr("Uses the effect chosen for the whole PC")
                size: 12
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }
        }

        UI.Button {
            id: button
            x: parent.width - 16 - width
            y: Math.round((parent.height - height) / 2)
            // The board's button: the usual 13px words, 32px tall.
            height: 32
            text: card.own ? qsTr("Back in sync") : qsTr("Set on its own")
            onClicked: card.toggled(!card.own)
        }
    }
}
