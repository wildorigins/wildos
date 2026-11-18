import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Colours: the six Wild OS accents. The choice is written as kdeglobals'
// AccentColor through plasma-apply-colorscheme, so every app follows it.
// Light or dark is chosen in Personalisation.
PageScroll {
    id: page

    Preview {
        Layout.preferredWidth: 320
        Layout.preferredHeight: 190
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Accent colour")
        }

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Shows on buttons, switches that are on, selections and the security shield.")
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.Card {
            Layout.fillWidth: true
            padding: 18
            clipContent: false

            RowLayout {
                spacing: 20

                AccentPicker {
                    Layout.leftMargin: 5
                    Layout.topMargin: 5
                    Layout.bottomMargin: 5
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: {
                        const chosen = UI.Style.accents.find(a => Qt.colorEqual(a.colour, System.accent));
                        return chosen ? chosen.name : Themes.accentName(System.accent) || qsTr("Your own colour");
                    }
                    color: UI.Style.inkMuted
                }
            }
        }
    }
}
