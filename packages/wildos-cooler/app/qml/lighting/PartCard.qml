import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// One part on the overview (10.11): a dark card with the part's name and
// how many zones it has, and under it every zone drawn in its shape with
// its lights as they are now. Zones with no lights yet are left out.
// Clicking the card opens the part.
T.AbstractButton {
    id: card

    property var part: ({})
    property var colours: []
    property string note

    readonly property var shown: (part.zones || []).filter(z => z.lights > 0)

    implicitHeight: 1 + 38 + 124 + 1
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: note

    background: Rectangle {
        radius: UI.Style.radiusPanel
        color: LightWords.stage
        border.width: 1
        border.color: card.hovered ? LightWords.stageRimLit : LightWords.stageRim
        Behavior on border.color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: card.visualFocus
            targetRadius: UI.Style.radiusPanel
        }
    }

    contentItem: Item {
        LightIcon {
            x: 14
            y: 1 + (38 - 15) / 2
            name: LightWords.icon(card.part.kind)
            size: 15
            strokeWidth: 2
            color: LightWords.stageMuted
        }

        Words {
            id: name
            x: 14 + 15 + 10
            y: 1 + (38 - height) / 2
            width: Math.min(implicitWidth, parent.width - x - 14 - sub.width - 10)
            text: card.text
            size: 13
            font.weight: UI.Style.weightBold
            color: LightWords.stageInk
            elide: Text.ElideRight
        }

        Words {
            id: sub
            x: parent.width - 14 - width
            y: 1 + (38 - height) / 2
            text: card.note
            size: 11.5
            color: LightWords.stageFaint
        }

        Item {
            id: body
            x: 1
            y: 39
            width: parent.width - 2
            height: 118
            clip: true

            Row {
                id: zones
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - height) / 2)
                spacing: 16
                // A part with more lights than the card is wide is drawn
                // smaller rather than cut off.
                scale: Math.min(1, (body.width - 24) / Math.max(1, implicitWidth), body.height / Math.max(1, implicitHeight))

                Repeater {
                    model: card.shown

                    Row {
                        id: entry

                        required property var modelData
                        required property int index
                        readonly property bool sectioned: modelData.shape !== "single" && modelData.split > 1 && modelData.lights > 1
                        readonly property bool afterSectioned: index > 0 && (sectioned || (card.shown[index - 1].shape !== "single" && card.shown[index - 1].split > 1))

                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 16

                        Rectangle {
                            visible: entry.afterSectioned
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 70
                            color: LightWords.stageEdge
                        }

                        ZoneLights {
                            anchors.verticalCenter: parent.verticalCenter
                            zone: entry.modelData
                            colours: card.colours[entry.modelData.index] || []
                            named: true
                            known: card.part.controlled
                            maxLength: 260
                        }
                    }
                }
            }
        }
    }
}
