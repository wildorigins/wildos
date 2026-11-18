import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// The Screen page (10.9, and 10.14 in the Overclock theme): the cooler with
// the chosen face on it, large and moving, the twelve faces to choose from,
// and the screen's settings. Choosing a face sends it to the cooler at
// once. Every colour is the theme's, so the page follows whichever theme
// is on.
Page {
    id: page

    title: qsTr("Screen")

    // The left column keeps the board's 372px while the faces have room
    // beside it; in a narrower window the faces go under it.
    readonly property bool wide: innerWidth >= 372 + 22 + 560

    // One clock for every face on the page, stepped as often as the
    // cooler's own screen takes a frame (about 13 a second). Only the large
    // preview and a face under the pointer follow it.
    property real clock: 0
    property double clockStart: 0

    Timer {
        interval: 77
        repeat: true
        running: CoolerApp.facesAvailable && page.visible && Qt.application.state === Qt.ApplicationActive
        onRunningChanged: if (running) page.clockStart = Date.now() - page.clock * 1000
        onTriggered: page.clock = (Date.now() - page.clockStart) / 1000
    }

    Item {
        width: parent.width
        height: page.wide ? Math.max(left.height, right.height) : left.height + 14 + right.height

        Column {
            id: left
            width: page.wide ? 372 : parent.width
            spacing: 14

            UI.Card {
                width: parent.width
                radius: UI.Style.radiusPanel
                clipContent: false
                padding: 1

                contentItem: Item {
                    implicitHeight: 22 + 308 + 14 + caption.height + 18

                    Bezel {
                        y: 22
                        anchors.horizontalCenter: parent.horizontalCenter
                        size: 308
                        faceId: CoolerApp.face
                        time: page.clock
                    }

                    Column {
                        id: caption
                        y: 22 + 308 + 14
                        width: parent.width
                        spacing: 2

                        Words {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: Format.faceName(CoolerApp.face)
                            size: 16
                            font.weight: UI.Style.weightBold
                        }

                        Words {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("On the cooler now, with live readings")
                            size: 12.5
                            color: UI.Style.inkMuted
                        }
                    }
                }
            }

            UI.Card {
                width: parent.width
                radius: UI.Style.radiusPanel
                clipContent: false
                padding: 1

                contentItem: Column {
                    OptionRow {
                        iconName: "brightness"
                        title: qsTr("Brightness")

                        UI.Slider {
                            width: 150
                            flat: true
                            value: CoolerApp.brightness
                            onMoved: CoolerApp.brightness = Math.round(value)
                        }
                    }

                    OptionRow {
                        iconName: "rotate"
                        title: qsTr("Rotate")

                        Segmented {
                            accessibleName: qsTr("Rotate")
                            options: [
                                { value: 0, text: qsTr("0°") },
                                { value: 90, text: qsTr("90°") },
                                { value: 180, text: qsTr("180°") },
                                { value: 270, text: qsTr("270°") }
                            ]
                            value: CoolerApp.rotation
                            onChosen: value => CoolerApp.rotation = value
                        }
                    }

                    OptionRow {
                        iconName: "cpu"
                        title: qsTr("Temperature")
                        // Without a graphics card temperature the GPU's number
                        // stays off, so say why rather than seem to ignore the choice.
                        description: CoolerApp.temperature !== "cpu" && CoolerApp.gpuTemperature < 0
                                     ? qsTr("No GPU temperature found on this PC")
                                     : CoolerApp.temperature === "both" ? qsTr("Side by side, Last minute takes turns") : ""

                        Segmented {
                            accessibleName: qsTr("Temperature")
                            options: [
                                { value: "cpu", text: qsTr("CPU") },
                                { value: "gpu", text: qsTr("GPU") },
                                { value: "both", text: qsTr("Both") }
                            ]
                            value: CoolerApp.temperature
                            onChosen: value => CoolerApp.temperature = value
                        }
                    }
                }
            }
        }

        Column {
            id: right
            x: page.wide ? left.width + 22 : 0
            y: page.wide ? 0 : left.height + 14
            width: parent.width - x
            spacing: 12

            Item {
                width: parent.width
                height: Math.max(facesHeading.implicitHeight, facesHint.implicitHeight)

                Words {
                    id: facesHeading
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Faces")
                    size: 14
                    font.weight: UI.Style.weightSemiBold
                    Accessible.role: Accessible.Heading
                }

                Words {
                    id: facesHint
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - facesHeading.width - 12)
                    text: qsTr("Click one to send it to the cooler")
                    size: 12.5
                    color: UI.Style.inkMuted
                }
            }

            Grid {
                id: grid
                width: parent.width
                columns: width >= 600 ? 6 : width >= 400 ? 4 : 3
                spacing: 4

                Repeater {
                    model: Format.faces

                    FaceTile {
                        required property var modelData
                        width: (grid.width - (grid.columns - 1) * grid.spacing) / grid.columns
                        text: modelData.name
                        faceId: modelData.id
                        current: CoolerApp.face === modelData.id
                        clock: page.clock
                        onClicked: CoolerApp.face = modelData.id
                    }
                }
            }

            UI.Card {
                width: parent.width
                radius: UI.Style.radiusPanel
                clipContent: false
                padding: 1

                contentItem: Column {
                    OptionRow {
                        iconName: "mark"
                        title: qsTr("Show the Wild OS mark")
                        description: qsTr("A small W on the face")

                        UI.Toggle {
                            checked: CoolerApp.showMark
                            onToggled: CoolerApp.showMark = checked
                        }
                    }

                    OptionRow {
                        iconName: "palette"
                        title: qsTr("Use my theme colours")
                        description: qsTr("Faces change when you change theme")

                        UI.Toggle {
                            checked: CoolerApp.themeColours
                            onToggled: CoolerApp.themeColours = checked
                        }
                    }

                    OptionRow {
                        iconName: "moon"
                        title: qsTr("Dim when the PC is idle")
                        description: qsTr("After ten minutes, to protect the screen")

                        UI.Toggle {
                            checked: CoolerApp.dimWhenIdle
                            onToggled: CoolerApp.dimWhenIdle = checked
                        }
                    }

                    OptionRow {
                        iconName: "warning"
                        title: qsTr("Switch to the red warning when")
                        description: qsTr("Overrides the face you chose until it cools")
                        stacked: width < 690

                        UI.ComboBox {
                            id: liquidLimit
                            width: 176
                            height: 32
                            accessibleName: qsTr("Switch to the red warning when the liquid is above")
                            model: [45, 50, 55, 60].map(t => ({ value: t, text: qsTr("Liquid is above %1").arg(Format.degrees(t)) }))
                            currentIndex: model.findIndex(o => o.value === CoolerApp.warnLiquidAbove)
                            onActivated: CoolerApp.warnLiquidAbove = currentValue
                        }

                        Item {
                            width: 18
                            height: 1
                        }

                        UI.ComboBox {
                            width: 196
                            height: 32
                            accessibleName: qsTr("Switch to the red warning when the CPU is at its limit for")
                            model: [
                                { value: 10, text: qsTr("CPU at its limit for 10 s") },
                                { value: 30, text: qsTr("CPU at its limit for 30 s") },
                                { value: 60, text: qsTr("CPU at its limit for 1 min") },
                                { value: 120, text: qsTr("CPU at its limit for 2 min") }
                            ]
                            currentIndex: model.findIndex(o => o.value === CoolerApp.warnCpuLimitSeconds)
                            onActivated: CoolerApp.warnCpuLimitSeconds = currentValue
                        }
                    }
                }
            }
        }
    }
}
