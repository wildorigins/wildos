import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// One zone of a part (10.15): its name to change, the name the part
// reports it under, its lights as they are now, and at the right the
// shape it is shown as, how many lights are plugged in and in how many
// sections where the part cannot count them itself, and Flash.
//
// In a window too narrow for the three side by side, the controls go
// under the lights.
Item {
    id: row

    property var part: ({})
    property var zone: ({})
    property var colours: []

    readonly property bool empty: (zone.lights || 0) === 0
    // 236px on the board. Words that run longer in another language widen
    // it rather than being cut short, by the same amount on every row.
    readonly property real controlsWidth: Math.max(236, ...Array.from(controls.children).filter(c => c.needed !== undefined).map(c => Math.ceil(c.needed)))
    readonly property bool wide: width >= 16 + 150 + 18 + 220 + 18 + controlsWidth + 16
    readonly property real stageWidth: wide ? width - 32 - 150 - 18 - 18 - controlsWidth : width - 32

    signal renamed(string name)
    signal shaped(string shape)
    signal counted(int count)
    signal sectioned(int sections)
    signal flashed()

    width: parent ? parent.width : implicitWidth
    implicitHeight: 1 + (wide ? Math.max(96, 24 + Math.max(names.height, stage.height, controls.height))
                              : 12 + names.height + 12 + stage.height + 12 + controls.height + 12)

    // A label with its control at the right. An inline component cannot
    // see this file's ids, so it takes its width from where it is put.
    component Line: Item {
        id: line

        property string label
        default property alias control: holder.data
        // The width at which the label is not cut short.
        readonly property real needed: text.implicitWidth + 10 + holder.width

        width: parent ? parent.width : 0
        height: Math.max(text.height, holder.height)

        Words {
            id: text
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - holder.width - 10
            text: line.label
            size: 12
            color: UI.Style.inkMuted
            elide: Text.ElideRight
        }

        Row {
            id: holder
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        width: parent.width
        height: 1
        color: UI.Style.borderSubtle
    }

    Column {
        id: names
        x: 16
        y: row.wide ? 1 + (row.height - 1 - height) / 2 : 13
        width: 150
        spacing: 6

        NameField {
            name: row.zone.name || ""
            accessibleName: qsTr("Name of %1").arg(row.zone.reported || "")
            onRenamed: name => row.renamed(name)
        }

        Words {
            width: parent.width
            text: row.empty && row.zone.resizable ? qsTr("Strip connector") : qsTr("Reported as %1").arg(row.zone.reported || "")
            size: 12
            color: UI.Style.inkMuted
            elide: Text.ElideRight
        }
    }

    Item {
        id: stage
        x: row.wide ? 16 + 150 + 18 : 16
        y: row.wide ? 1 + (row.height - 1 - height) / 2 : names.y + names.height + 12
        width: row.stageWidth
        height: row.empty ? sockets.height : lights.height * lights.scale

        // The board draws the lights straight on the card, which is dark.
        // On a pale card they get the dark stage behind them, or a white
        // light could not be told from the card.
        Rectangle {
            visible: !UI.Style.dark && (!row.empty || row.zone.resizable === true)
            anchors.centerIn: parent
            width: (row.empty ? sockets.width : lights.width * lights.scale) + 20
            height: parent.height + 12
            radius: 8
            color: LightWords.stage
        }

        ZoneLights {
            id: lights
            anchors.centerIn: parent
            zone: row.zone
            colours: row.colours
            sizing: "large"
            onStage: !UI.Style.dark
            maxLength: row.stageWidth
            scale: Math.min(1, row.stageWidth / Math.max(1, implicitWidth))
        }

        LightStrip {
            id: sockets
            visible: row.empty && row.zone.resizable === true
            anchors.centerIn: parent
            unit: 12
            sockets: Math.max(4, Math.min(14, Math.floor((row.stageWidth - 30) / 16.2) + 1))
        }
    }

    Column {
        id: controls
        x: row.wide ? row.width - 16 - width : 16
        y: row.wide ? 1 + (row.height - 1 - height) / 2 : stage.y + stage.height + 12
        width: row.controlsWidth
        spacing: 8

        Line {
            visible: !row.empty
            label: qsTr("Shown as")

            Segmented {
                accessibleName: qsTr("Shown as")
                options: [
                    { value: "strip", text: qsTr("Strip") },
                    { value: "ring", text: qsTr("Ring") },
                    { value: "single", text: qsTr("Single") }
                ]
                value: row.zone.shape
                onChosen: value => row.shaped(value)
            }
        }

        Line {
            visible: row.zone.resizable === true
            label: qsTr("Lights plugged in")

            Stepper {
                accessibleName: qsTr("Lights plugged in")
                value: row.zone.lights || 0
                from: row.zone.min || 0
                to: row.zone.max || 0
                onMoved: value => row.counted(value)
            }
        }

        Line {
            visible: row.zone.resizable === true && !row.empty && row.zone.shape !== "single"
            label: qsTr("Split into")

            Stepper {
                accessibleName: qsTr("Split into")
                value: row.zone.split || 1
                from: 1
                to: Math.min(12, row.zone.lights || 1)
                onMoved: value => row.sectioned(value)
            }
        }

        Line {
            visible: !row.empty
            label: row.zone.resizable ? qsTr("Find it") : qsTr("%1, fixed").arg(LightWords.lights(row.zone.lights || 0))

            FlashButton {
                flashing: row.zone.flashing === true
                onClicked: row.flashed()
            }
        }

        Words {
            visible: row.empty
            width: parent.width
            text: row.zone.resizable === true ? LightWords.cannotCount(row.part.kind) : qsTr("No lights are reported here.")
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
    }
}
