import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// One part and its zones.
//
// A part with colour (10.15) lists each zone with its name, shape, count
// and Flash. A part whose lights can only do white (10.16) shows them
// large, with only the effects it can show. A part Wild OS can see but
// does not control says so plainly and offers nothing to change.
//
// Under the zones: whether the part follows the whole PC's effect or has
// its own, and when it has its own, the effects and their settings.
Column {
    id: view

    property var part: ({})
    property string title
    // The part's lights as they are now, zone by zone.
    property var colours: []

    readonly property bool controlled: part.controlled === true
    readonly property bool white: part.white === true
    readonly property bool rows: controlled && !white
    readonly property var zones: part.zones || []
    readonly property bool allFlashing: zones.some(z => z.lights > 0) && zones.every(z => z.lights === 0 || z.flashing)

    spacing: 10

    UI.Card {
        width: parent.width
        radius: UI.Style.radiusPanel
        padding: 1

        contentItem: Column {
            Item {
                width: parent.width
                height: Math.max(54, heading.height + 16)

                LightIcon {
                    x: 16
                    y: Math.round((parent.height - height) / 2)
                    name: LightWords.icon(view.part.kind)
                    size: 19
                    color: UI.Style.ink
                }

                Column {
                    id: heading
                    x: 16 + 19 + 13
                    width: parent.width - x - 16 - side.width - 13
                    y: Math.round((parent.height - height) / 2)
                    spacing: 1

                    Words {
                        width: parent.width
                        text: view.title
                        size: 15
                        font.weight: UI.Style.weightBold
                        elide: Text.ElideRight
                    }

                    Words {
                        width: parent.width
                        text: !view.controlled ? (view.title === view.part.name ? LightWords.zones(view.zones.length) : view.part.name || "")
                            : qsTr("%1 · %2 · %3").arg(LightWords.zones(view.zones.length)).arg(LightWords.lights(view.part.lights || 0)).arg(LightWords.reportedBy(view.part.kind))
                        size: 12
                        color: UI.Style.inkMuted
                        elide: Text.ElideRight
                    }
                }

                Row {
                    id: side
                    x: parent.width - 16 - width
                    anchors.verticalCenter: parent.verticalCenter

                    FlashButton {
                        visible: view.rows
                        idleText: qsTr("Flash all")
                        flashing: view.allFlashing
                        onClicked: Lighting.flash(view.part.key, -1)
                    }

                    Words {
                        visible: !view.rows
                        text: view.controlled ? qsTr("White only") : qsTr("Not controlled")
                        size: 12
                        color: UI.Style.inkMuted
                    }
                }
            }

            Repeater {
                model: view.rows ? view.zones : []

                ZoneRow {
                    required property var modelData
                    part: view.part
                    zone: modelData
                    colours: view.colours[modelData.index] || []
                    onRenamed: name => Lighting.setZoneName(view.part.key, modelData.index, name)
                    onShaped: shape => Lighting.setZoneShape(view.part.key, modelData.index, shape)
                    onCounted: count => Lighting.setZoneCount(view.part.key, modelData.index, count)
                    onSectioned: sections => Lighting.setZoneSplit(view.part.key, modelData.index, sections)
                    onFlashed: Lighting.flash(view.part.key, modelData.index)
                }
            }

            Rectangle {
                visible: !view.rows
                width: parent.width
                height: 1
                color: UI.Style.borderSubtle
            }

            Rectangle {
                id: stage
                visible: !view.rows
                width: parent.width
                height: columns.height
                color: LightWords.stage
                bottomLeftRadius: UI.Style.radiusPanel - 1
                bottomRightRadius: UI.Style.radiusPanel - 1

                Row {
                    id: columns

                    Repeater {
                        model: view.rows ? [] : view.zones

                        Item {
                            id: column

                            required property var modelData
                            required property int index

                            // The columns share what the 1px lines
                            // between them leave.
                            width: (stage.width - (view.zones.length - 1)) / Math.max(1, view.zones.length) + (index > 0 ? 1 : 0)
                            height: 16 + inner.height + 14

                            Rectangle {
                                visible: column.index > 0
                                width: 1
                                height: parent.height
                                color: LightWords.stageEdge
                            }

                            Column {
                                id: inner
                                x: column.index > 0 ? 1 : 0
                                y: 16
                                width: parent.width - x
                                spacing: 10

                                ZoneLights {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    zone: column.modelData
                                    colours: view.colours[column.modelData.index] || []
                                    sizing: "white"
                                    known: view.controlled
                                    maxLength: column.width - 32
                                }

                                Column {
                                    width: parent.width
                                    spacing: 2

                                    Words {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: column.modelData.name
                                        size: 14
                                        font.weight: UI.Style.weightBold
                                        color: LightWords.stageInk
                                    }

                                    Words {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: {
                                            const shape = column.modelData.shape === "single" || column.modelData.lights === 1
                                                ? qsTr("Single light") : LightWords.lights(column.modelData.lights);
                                            return column.modelData.white ? qsTr("%1 · %2").arg(shape).arg(qsTr("white only")) : shape;
                                        }
                                        size: 12
                                        color: LightWords.stageMuted
                                    }
                                }

                                FlashButton {
                                    visible: view.controlled
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    flashing: column.modelData.flashing === true
                                    onClicked: Lighting.flash(view.part.key, column.modelData.index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    LightsNotice {
        visible: !view.controlled
        width: parent.width
        iconName: "eye"
        title: qsTr("Seen, not controlled")
        text: view.part.why === "bus"
            ? qsTr("Wild OS can see this part is here, but it does not control its lights. They are reached over a connection inside the PC where a wrong message can damage hardware, so Cooler never uses it and leaves these lights exactly as they are.")
            : qsTr("Wild OS can see this part is here, but it has no safe way to set its lights, so Cooler leaves them exactly as they are.")
    }

    SyncCard {
        visible: view.controlled
        width: parent.width
        own: view.part.own === true
        onToggled: own => Lighting.setPartOwn(view.part.key, own)
    }

    EffectGrid {
        visible: view.controlled && view.part.own === true
        width: parent.width
        effects: view.part.effects || []
        current: view.part.effect || ""
        white: view.white
        onChosen: effect => Lighting.setPart(view.part.key, effect, view.part.brightness, view.part.speed)
    }

    LightOptions {
        visible: view.controlled && view.part.own === true
        width: parent.width
        brightness: view.part.brightness || 0
        speed: view.part.speed || 0
        speedNote: view.part.effect === "breathe" ? qsTr("How fast it breathes") : qsTr("How fast the colours move")
        onBrightnessMoved: value => Lighting.setPart(view.part.key, view.part.effect, value, view.part.speed)
        onSpeedMoved: value => Lighting.setPart(view.part.key, view.part.effect, view.part.brightness, value)
    }

    // The board's line sits 2px nearer the card above than a line box of
    // its own would put it.
    Item {
        visible: view.controlled
        width: parent.width
        height: foot.height - 2

        Words {
            id: foot
            x: 4
            y: -2
            width: parent.width - 8
            text: view.white
                ? qsTr("These lights can only do white, so effects that need colour are not offered. Left in sync, they follow the brightness of the rest and ignore the colour.")
                : view.part.still === true
                ? qsTr("Flash shows a zone white for a few seconds so you can see which one it is. This part keeps what it is sent in its own memory, so effects that move are shown standing still on it instead of being sent many times a second. Names, shapes and counts are kept on this PC.")
                : qsTr("Flash blinks a zone white for a few seconds so you can see which one it is. Names, shapes and counts are kept on this PC.")
            size: 12
            line: 1.45
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
    }
}
