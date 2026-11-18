import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// One row of the list of parts (10.11): 54px, the part's icon, its name
// over a line saying what it has, and four blocks in its lights' colours
// as they are now. The chosen row sits on the selected fill with the
// accent bar at its left.
T.AbstractButton {
    id: row

    property string iconName
    property string summary
    property bool current: false
    // Four colours for the blocks; empty draws them as outlines, for a
    // part whose lights cannot be read.
    property var swatch: []
    property bool divider: true

    width: parent ? parent.width : implicitWidth
    implicitHeight: (divider ? 1 : 0) + Math.max(54, names.height + 16)
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.description: summary
    Accessible.selected: current

    background: Item {
        Rectangle {
            visible: row.divider
            width: parent.width
            height: 1
            color: UI.Style.borderSubtle
        }

        Rectangle {
            y: row.divider ? 1 : 0
            width: parent.width
            height: parent.height - y
            color: row.down ? UI.Style.surfacePressed : row.current ? UI.Style.surfaceSelected : row.hovered ? UI.Style.surfaceControl : "transparent"
            Behavior on color {
                ColorAnimation { duration: UI.Style.hoverDuration }
            }

            Rectangle {
                visible: row.current
                y: 12
                width: 3
                height: parent.height - 24
                radius: 2
                color: UI.Style.accent
            }

            UI.FocusRing {
                visible: row.visualFocus
                targetRadius: 4
            }
        }
    }

    contentItem: Item {
        Item {
            y: row.divider ? 1 : 0
            width: parent.width
            height: parent.height - y

            LightIcon {
                x: 16
                y: Math.round((parent.height - height) / 2)
                name: row.iconName
                color: row.current ? UI.Style.ink : UI.Style.inkMuted
            }

            Column {
                id: names
                x: 16 + 17 + 13
                width: parent.width - x - 13 - 40 - 14
                // Half pixels go down, as the board's do.
                y: Math.round((parent.height - height) / 2)
                spacing: 1

                Words {
                    width: parent.width
                    text: row.text
                    size: 13.5
                    font.weight: row.current ? UI.Style.weightBold : UI.Style.weightRegular
                    elide: Text.ElideRight
                }

                Words {
                    width: parent.width
                    text: row.summary
                    size: 11.5
                    color: UI.Style.inkMuted
                    elide: Text.ElideRight
                }
            }

            Row {
                x: parent.width - 14 - 40
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Repeater {
                    model: 4

                    Rectangle {
                        required property int index
                        readonly property bool known: row.swatch.length > 0
                        width: 7
                        height: 8
                        radius: index === 0 || index === 3 ? 2 : 0
                        color: known ? row.swatch[Math.min(index, row.swatch.length - 1)] : "transparent"
                        // On a pale card a white light needs an edge to
                        // be seen at all.
                        border.width: known && UI.Style.dark ? 0 : 1
                        border.color: known ? UI.Style.tone(0.2) : UI.Style.inkSubtle
                    }
                }
            }
        }
    }
}
