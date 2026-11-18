import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// The Lighting page (10.11, 10.15, 10.16): every light in this PC.
//
// On the left, the parts Wild OS knows of, with "Everything" above them.
// On the right, either all of them as they look now with the one effect
// that runs across them (10.11), or one part and its zones (10.15), which
// for a part that can only do white is 10.16.
//
// Each part reports its zones and how many lights each has. Where a light
// sits in the PC is not known and not drawn: a zone is a strip, a ring or
// a single light, as the person says.
//
// The window sets no light itself. See Lighting (app/src/lighting).
Flickable {
    id: page

    // "" for Everything, else a part's key.
    property string chosen: ""

    readonly property real innerWidth: width - 48
    readonly property bool wide: innerWidth >= 300 + 14 + 560
    readonly property bool ready: Lighting.state === "ready"
    // A part that is only seen reports no names for its lights.
    readonly property var parts: Lighting.parts.map(p => p.controlled || p.why !== "bus" ? p
        : Object.assign({}, p, { zones: p.zones.map(z => Object.assign({}, z, { name: LightWords.seenLight(z) })) }))
    readonly property int chosenIndex: parts.findIndex(p => p.key === chosen)
    readonly property var part: chosenIndex >= 0 ? parts[chosenIndex] : null
    readonly property int lightCount: parts.reduce((sum, p) => sum + (p.controlled ? p.lights : 0), 0)

    // Four colours for a row's blocks: from along the first run of lights
    // the part has, which is once through whatever effect it shows. A
    // ring shows its effect there and back, so half of it is enough.
    function sample(index) {
        const part = parts[index];
        const colours = Lighting.colours[index] || [];
        if (!part || !part.controlled) {
            return [];
        }
        for (const zone of part.zones) {
            const lit = colours[zone.index] || [];
            if (lit.length === 0) {
                continue;
            }
            const run = Math.ceil(lit.length / (zone.shape === "single" ? 1 : zone.split));
            const span = zone.shape === "ring" ? Math.floor(run / 2) + 1 : run;
            return [0, 1, 2, 3].map(i => lit[Math.round(i * (span - 1) / 3)]);
        }
        return [];
    }

    function sampleAll() {
        const first = parts.findIndex(p => p.controlled && !p.white && p.lights > 0);
        return sample(first >= 0 ? first : parts.findIndex(p => p.controlled && p.lights > 0));
    }

    contentWidth: width
    contentHeight: column.height + 40
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    T.ScrollBar.vertical: UI.ScrollBar {}

    onPartChanged: if (chosen !== "" && part === null) chosen = ""

    Binding {
        target: Lighting
        property: "watching"
        value: page.visible && Window.window !== null && Window.window.visible
    }
    Component.onDestruction: Lighting.watching = false

    Column {
        id: column
        x: 24
        y: 20
        width: page.innerWidth
        spacing: 12

        Item {
            width: parent.width
            height: heading.height

            Words {
                id: heading
                text: qsTr("Lighting")
                size: 26
                font.weight: UI.Style.weightBold
                Accessible.role: Accessible.Heading
            }

            Words {
                anchors.left: heading.right
                anchors.leftMargin: 14
                anchors.baseline: heading.baseline
                width: parent.width - heading.width - 14
                elide: Text.ElideRight
                text: !page.ready || page.part === null ? qsTr("Every light in this PC, as it looks now. One effect runs across all of it.")
                    : !page.part.controlled ? qsTr("A part Wild OS can see but does not control. Its lights are left as they are.")
                    : page.part.white ? qsTr("A part whose lights can only do white. Only the effects it can show are offered.")
                    : qsTr("One part and its zones. Name each one, say what shape it is, and count the lights where the part cannot.")
                size: 13
                color: UI.Style.inkMuted
            }
        }

        LightsNotice {
            visible: !page.ready || page.parts.length === 0
            width: parent.width
            iconName: Lighting.state === "ready" ? "info" : "light"
            title: Lighting.state === "off" ? qsTr("Lighting is turned off")
                : Lighting.state === "starting" || Lighting.state === "away" ? qsTr("Looking for lights")
                : Lighting.state === "failed" ? qsTr("The lights could not be reached")
                : Lighting.state === "missing" ? qsTr("Lighting is not set up on this PC")
                : qsTr("No lights found")
            text: Lighting.state === "off" ? qsTr("Turn it on to see and set the lights of this PC. It is the same switch as Lighting on RGB keyboards and mice in Settings.")
                : Lighting.state === "starting" || Lighting.state === "away" ? qsTr("This takes a few seconds.")
                : Lighting.state === "failed" ? qsTr("OpenRGB, which Wild OS sets lights through, did not start. Turn the lighting off and on again in Settings.")
                : Lighting.state === "missing" ? qsTr("Wild OS sets lights through OpenRGB, which is not installed or not running on this PC. Cooler touches no light without it.")
                : qsTr("No part of this PC that Wild OS can safely control has lights. Lights on memory and on most graphics cards are left alone on purpose: they are reached over a connection inside the PC where a wrong message can damage hardware.")
            action: Lighting.state === "off" ? qsTr("Turn on") : ""
            onActed: Lighting.turnOn()
        }

        Item {
            visible: page.ready && page.parts.length > 0
            width: parent.width
            height: page.wide ? Math.max(side.height, detail.height) : side.height + 14 + detail.height

            Column {
                id: side
                width: page.wide ? 300 : parent.width
                spacing: 12

                UI.Card {
                    width: parent.width
                    radius: UI.Style.radiusPanel
                    padding: 1

                    contentItem: Column {
                        PartRow {
                            divider: false
                            iconName: "everything"
                            text: qsTr("Everything")
                            summary: qsTr("%1 · %2").arg(LightWords.parts(page.parts.length)).arg(LightWords.lights(page.lightCount))
                            current: page.part === null
                            swatch: page.sampleAll()
                            onClicked: page.chosen = ""
                        }

                        Repeater {
                            model: page.parts

                            PartRow {
                                required property var modelData
                                required property int index
                                iconName: LightWords.icon(modelData.kind)
                                text: LightWords.title(modelData, page.parts)
                                summary: LightWords.summary(modelData)
                                current: page.chosen === modelData.key
                                swatch: page.sample(index)
                                onClicked: page.chosen = modelData.key
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: note.height + 4

                    LightIcon {
                        x: 4
                        y: 2
                        name: "info"
                        size: 15
                        strokeWidth: 2
                        color: UI.Style.inkSubtle
                    }

                    Words {
                        id: note
                        x: 4 + 15 + 10
                        y: 1
                        width: parent.width - x - 4
                        text: qsTr("Each part tells Wild OS which zones it has and how many lights are in each. They are drawn here as strips, rings and single lights, not as a map of where they sit.")
                        size: 12
                        line: 1.45
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }
            }

            Item {
                id: detail
                x: page.wide ? 300 + 14 : 0
                y: page.wide ? 0 : side.height + 14
                width: page.wide ? parent.width - 300 - 14 : parent.width
                height: page.part === null ? everything.height : one.height

                Column {
                    id: everything
                    visible: page.part === null
                    width: parent.width
                    spacing: 10

                    Grid {
                        id: cards
                        width: parent.width
                        columns: width >= 600 ? 2 : 1
                        spacing: 10

                        Repeater {
                            model: page.parts

                            PartCard {
                                required property var modelData
                                required property int index
                                width: (cards.width - cards.spacing * (cards.columns - 1)) / cards.columns
                                part: modelData
                                text: LightWords.title(modelData, page.parts)
                                note: !modelData.controlled ? LightWords.summary(modelData)
                                    : modelData.white ? LightWords.summary(modelData)
                                    : LightWords.zones(modelData.zones.length)
                                colours: Lighting.colours[index] || []
                                onClicked: page.chosen = modelData.key
                            }
                        }
                    }

                    EffectGrid {
                        width: parent.width
                        effects: Lighting.everything.effects || []
                        current: Lighting.everything.effect || ""
                        onChosen: effect => Lighting.setEverything(effect, Lighting.everything.brightness, Lighting.everything.speed)
                    }

                    LightOptions {
                        width: parent.width
                        brightness: Lighting.everything.brightness || 0
                        speed: Lighting.everything.speed || 0
                        speedNote: Lighting.everything.effect === "breathe" ? qsTr("How fast it breathes") : qsTr("How fast the colours move")
                        lockRow: true
                        offWhenLocked: Lighting.everything.offWhenLocked === true
                        onBrightnessMoved: value => Lighting.setEverything(Lighting.everything.effect, value, Lighting.everything.speed)
                        onSpeedMoved: value => Lighting.setEverything(Lighting.everything.effect, Lighting.everything.brightness, value)
                        onLockToggled: off => Lighting.setOffWhenLocked(off)
                    }
                }

                PartView {
                    id: one
                    visible: page.part !== null
                    width: parent.width
                    part: page.part || ({})
                    title: page.part ? LightWords.title(page.part, page.parts) : ""
                    colours: page.chosenIndex >= 0 ? Lighting.colours[page.chosenIndex] || [] : []
                }
            }
        }
    }
}
