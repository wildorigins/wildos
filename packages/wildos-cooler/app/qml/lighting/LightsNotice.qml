import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// What the Lighting page says when there are no lights to show: the
// lighting is off, still starting, or there is nothing Wild OS can safely
// control. One card with an icon, a line, a paragraph and at most one
// button.
UI.Card {
    id: notice

    property string iconName: "light"
    property string title
    property string text
    property string action

    signal acted()

    radius: UI.Style.radiusPanel
    clipContent: false
    leftPadding: 17
    rightPadding: 17
    topPadding: 15
    bottomPadding: 15

    contentItem: Item {
        implicitHeight: Math.max(words.height, button.visible ? button.height : 0)

        LightIcon {
            y: 1
            name: notice.iconName
            size: 18
        }

        Column {
            id: words
            x: 29
            width: parent.width - x - (button.visible ? button.width + 16 : 0)
            spacing: 3

            Words {
                width: parent.width
                text: notice.title
                size: 13.5
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.Wrap
            }

            Words {
                width: parent.width
                text: notice.text
                size: 12.5
                line: 1.5
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }
        }

        UI.Button {
            id: button
            visible: notice.action.length > 0
            x: parent.width - width
            anchors.verticalCenter: parent.verticalCenter
            size: "small"
            text: notice.action
            onClicked: notice.acted()
        }
    }
}
