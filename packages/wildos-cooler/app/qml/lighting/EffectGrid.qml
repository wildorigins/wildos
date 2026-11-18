import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// The six effects, three to a row (10.11). The chosen one takes the
// accent. One a part cannot show is faded and says why (10.16 "Needs
// colour"); it cannot be chosen.
Grid {
    id: grid

    // [{ id, offered, why }]
    property var effects: []
    property string current
    // For a part that can only do white: the samples are drawn in white.
    property bool white: false

    signal chosen(string effect)

    columns: width >= 520 ? 3 : 2
    spacing: 8

    function sample(id) {
        const lit = white ? Qt.rgba(1, 1, 1, 1) : UI.Style.accent;
        switch (id) {
        case "theme": return LightWords.themeStops;
        case "taskbar": return LightWords.spectrum;
        case "temperature": return LightWords.temperature;
        case "breathe": return [Colours.alpha(lit, white ? 0.12 : 0.15), lit, Colours.alpha(lit, white ? 0.12 : 0.15)];
        case "steady": return [lit];
        }
        return [UI.Style.tone(0.08)];
    }

    function reason(why) {
        return why === "colour" ? qsTr("Needs colour") : why === "sensor" ? qsTr("No temperature reading") : qsTr("Not on this part");
    }

    Repeater {
        model: grid.effects

        T.AbstractButton {
            id: card

            required property var modelData
            readonly property bool current: modelData.offered && grid.current === modelData.id

            width: (grid.width - grid.spacing * (grid.columns - 1)) / grid.columns
            implicitHeight: 2 + 10 + 7 + 8 + label.height + 10
            padding: 0
            enabled: modelData.offered
            opacity: modelData.offered ? 1 : 0.45
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            text: LightWords.effectName(modelData.id)
            Accessible.role: Accessible.RadioButton
            Accessible.name: text
            Accessible.description: modelData.offered ? "" : grid.reason(modelData.why)
            Accessible.checked: current
            onClicked: grid.chosen(modelData.id)

            background: Rectangle {
                radius: 9
                color: card.current ? UI.Style.accentAlpha(0.10, 0.09) : card.down ? UI.Style.surfacePressed : card.hovered ? UI.Style.surfaceHover : UI.Style.surfaceRaised
                border.width: 1
                border.color: card.current ? UI.Style.accent : UI.Style.borderSubtle
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                UI.FocusRing {
                    visible: card.visualFocus
                    targetRadius: 9
                }
            }

            contentItem: Item {
                ColourBar {
                    x: 13
                    y: 11
                    width: parent.width - 26
                    height: 7
                    colours: grid.sample(card.modelData.id)
                }

                Words {
                    id: label
                    x: 13
                    y: 11 + 7 + 8
                    text: card.text
                    size: 13
                    font.weight: card.current ? UI.Style.weightBold : 450
                }

                Words {
                    visible: !card.modelData.offered
                    anchors.left: label.right
                    anchors.leftMargin: 8
                    anchors.baseline: label.baseline
                    width: Math.max(0, parent.width - 13 - x)
                    elide: Text.ElideRight
                    text: grid.reason(card.modelData.why)
                    size: 11.5
                    color: UI.Style.inkMuted
                }
            }
        }
    }
}
