import QtQuick
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// The six Wild OS accents as Personalisation.dc.html draws them: 32px
// discs 12px apart, the chosen one ringed in its own colour with a gap of
// window between. Home, Personalisation and Colours all use this one picker.
// Picking writes kdeglobals through System.setAccent, so every app follows.
Row {
    id: picker

    property int size: 32

    spacing: 12
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Accent colour")

    Repeater {
        model: UI.Style.accents

        T.AbstractButton {
            id: swatch

            required property var modelData
            readonly property bool selected: Qt.colorEqual(System.accent, modelData.colour)

            width: picker.size
            height: picker.size
            padding: 0
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.name
            Accessible.checked: selected
            onClicked: System.setAccent(modelData.colour)

            background: Rectangle {
                radius: width / 2
                color: swatch.down ? Qt.darker(swatch.modelData.colour, 1.08)
                     : swatch.hovered ? Qt.lighter(swatch.modelData.colour, 1.08)
                     : swatch.modelData.colour
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                Rectangle {
                    visible: swatch.selected
                    anchors.fill: parent
                    anchors.margins: -5
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: swatch.modelData.colour
                }

                UI.FocusRing {
                    visible: swatch.visualFocus
                    anchors.margins: swatch.selected ? -8 : -4
                    targetRadius: width / 2
                }
            }

            UI.Tooltip {
                text: swatch.modelData.name
                visible: swatch.hovered
            }
        }
    }
}
