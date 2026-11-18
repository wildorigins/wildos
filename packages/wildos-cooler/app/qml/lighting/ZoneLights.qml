import QtQuick
import WildOS.Cooler
import WildOS.UI as UI
import "LightIcons.js" as LightIcons

// One zone as the person has said it is shown: a strip, a ring or a single
// light, every light in its live colour. A zone split into sections is
// drawn section by section, each with its number (10.11: three fans, four
// sticks). Nothing here says where a light sits in the PC.
//
// `sizing` is "small" for the overview's cards (10.11), "large" for a
// part's own page (10.15) and "white" for the big lights of 10.16.
Row {
    id: view

    property var zone: ({})
    property var colours: []
    property string sizing: "small"
    // Writes the zone's name under a zone that is not split.
    property bool named: false
    // Whether Wild OS can tell what the lights are doing at all.
    property bool known: true
    property bool onStage: true
    property real maxLength: 360

    readonly property string shape: zone.shape || "strip"
    readonly property var runs: LightIcons.sections(zone.lights || 0, shape === "single" ? 1 : zone.split || 1)
    readonly property bool sectioned: runs.length > 1
    readonly property bool small: sizing === "small"
    readonly property real fullUnit: small ? 7.2 : 12
    readonly property real tallest: small ? 86 : 112

    spacing: small ? 16 : 14

    function start(section) {
        let at = 0;
        for (let i = 0; i < section; ++i) {
            at += runs[i];
        }
        return at;
    }

    Repeater {
        model: (view.zone.lights || 0) > 0 ? view.runs.length : 0

        Column {
            id: section

            required property int index
            readonly property int count: view.runs[index]
            readonly property var own: view.colours.slice(view.start(index), view.start(index) + count)
            readonly property string caption: view.sectioned
                ? (view.shape === "ring" ? qsTr("Fan %1").arg(index + 1) : String(index + 1))
                : view.named ? view.zone.name : ""

            spacing: 5

            Loader {
                anchors.horizontalCenter: parent.horizontalCenter
                sourceComponent: view.shape === "ring" ? ring : view.shape === "single" ? single : strip
            }

            Words {
                visible: section.caption.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: section.caption
                size: 11
                color: view.onStage ? LightWords.stageMuted : UI.Style.inkMuted
            }

            Component {
                id: strip

                LightStrip {
                    lights: section.count
                    colours: section.own
                    upright: view.sectioned
                    // A section stood on end is shrunk to the height the
                    // board gives its tallest drawing.
                    unit: view.sectioned ? Math.min(view.fullUnit, view.tallest / (1.35 * (section.count - 1) + 2.5)) : view.fullUnit
                    maxLength: view.sectioned ? 100000 : view.maxLength
                }
            }

            Component {
                id: ring

                LightRing {
                    lights: section.count
                    colours: section.own
                    // The board's sizes: a ring of ten lights is drawn a
                    // little smaller than one of eight.
                    size: view.small ? (section.count <= 8 ? 58 : section.count <= 12 ? 56 : 84) : (section.count <= 12 ? 78 : 104)
                }
            }

            Component {
                id: single

                LightSingle {
                    colour: section.own[0] || "black"
                    known: view.known
                    size: view.sizing === "white" ? 92 : 54
                }
            }
        }
    }
}
