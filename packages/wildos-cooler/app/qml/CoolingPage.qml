import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// The Cooling page (10.10): the readings, the four modes, the speed curve
// with its points to drag, and the options beside it. Fans that are not
// plugged into the cooler cannot be read or driven from here, so their
// tile, their curve and their options are left out and the page says so.
Page {
    id: page

    title: qsTr("Cooling")

    readonly property bool fans: CoolerApp.fansConnected
    readonly property bool wide: innerWidth >= 300 + 14 + 460
    // "fans" or "pump": the curve the chart shows.
    property string channel: "fans"
    readonly property string shown: fans ? channel : "pump"

    readonly property var modes: [
        { id: "quiet", name: qsTr("Quiet"), note: fans ? qsTr("Slower fans. Fine for everyday work.") : qsTr("Slower pump. Fine for everyday work.") },
        { id: "balanced", name: qsTr("Balanced"), note: qsTr("The default. Quiet until it matters.") },
        { id: "performance", name: qsTr("Performance"), note: qsTr("Coolest, and you will hear it.") },
        { id: "custom", name: qsTr("Your own"), note: qsTr("Drag the points on the chart.") }
    ]

    Row {
        id: tiles
        width: parent.width
        spacing: 12
        readonly property int count: page.fans ? 4 : 3
        readonly property real each: (width - spacing * (count - 1)) / count

        ReadingTile {
            width: tiles.each
            iconName: "cpu"
            iconColor: Colours.cpu
            name: qsTr("CPU")
            value: CoolerApp.cpuTemperature >= 0 ? Math.round(CoolerApp.cpuTemperature) : ""
            unit: CoolerApp.cpuTemperature >= 0 ? qsTr("°C") : ""
            note: CoolerApp.cpuTemperature >= 0 ? qsTr("Highest today %1").arg(Format.degrees(CoolerApp.cpuHighestToday)) : ""
        }

        ReadingTile {
            width: tiles.each
            iconName: "drop"
            iconColor: Colours.liquid
            name: qsTr("Liquid")
            value: CoolerApp.liquidTemperature >= 0 ? Math.round(CoolerApp.liquidTemperature) : ""
            unit: CoolerApp.liquidTemperature >= 0 ? qsTr("°C") : ""
            note: CoolerApp.roomTemperature >= 0 ? qsTr("Room is about %1").arg(Format.degrees(CoolerApp.roomTemperature)) : ""
        }

        ReadingTile {
            width: tiles.each
            iconName: "pump"
            iconColor: Colours.pump
            name: qsTr("Pump")
            value: Format.count(CoolerApp.pumpRpm)
            unit: qsTr("rpm")
            note: qsTr("%1 per cent").arg(CoolerApp.pumpPercent)
        }

        ReadingTile {
            visible: page.fans
            width: tiles.each
            iconName: "fan"
            iconColor: Colours.fans
            name: qsTr("Fans")
            value: Format.count(CoolerApp.fanRpm)
            unit: qsTr("rpm")
            note: qsTr("%1 per cent").arg(CoolerApp.fanPercent)
        }
    }

    Row {
        id: modeRow
        width: parent.width
        spacing: 10
        readonly property real each: (width - spacing * 3) / 4
        readonly property real tallest: Math.max(...Array.from(children).map(c => c.implicitHeight || 0))

        Repeater {
            model: page.modes

            ModeCard {
                required property var modelData
                width: modeRow.each
                height: modeRow.tallest
                text: modelData.name
                description: modelData.note
                current: CoolerApp.mode === modelData.id
                onClicked: CoolerApp.mode = modelData.id
            }
        }
    }

    Item {
        width: parent.width
        height: page.wide ? Math.max(chartCard.height, side.height) : chartCard.height + 14 + side.height

        UI.Card {
            id: chartCard
            width: page.wide ? parent.width - 300 - 14 : parent.width
            radius: UI.Style.radiusPanel
            clipContent: false
            leftPadding: 19
            rightPadding: 19
            topPadding: 17
            bottomPadding: 13

            contentItem: Item {
                implicitHeight: header.height + 10 + chart.height

                Item {
                    id: header
                    width: parent.width
                    height: Math.max(heading.height, channels.visible ? channels.height : 0)

                    Column {
                        id: heading
                        width: parent.width - (channels.visible ? channels.width : unconnected.width) - 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Words {
                            width: parent.width
                            text: page.shown === "fans" ? qsTr("Fan speed") : qsTr("Pump speed")
                            size: 14
                            font.weight: UI.Style.weightSemiBold
                        }

                        Words {
                            width: parent.width
                            text: page.shown === "fans"
                                ? qsTr("Follows the liquid, so the fans do not surge every time the CPU spikes.")
                                : qsTr("Follows the liquid, so the pump does not surge every time the CPU spikes.")
                            size: 12.5
                            color: UI.Style.inkMuted
                            wrapMode: Text.Wrap
                        }
                    }

                    Segmented {
                        id: channels
                        visible: page.fans
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        accessibleName: qsTr("Curve")
                        options: [
                            { value: "fans", text: qsTr("Fans") },
                            { value: "pump", text: qsTr("Pump") }
                        ]
                        value: page.channel
                        onChosen: value => page.channel = value
                    }

                    Row {
                        id: unconnected
                        visible: !page.fans
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        CoolerIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "fan"
                            size: 16
                            strokeWidth: 2
                        }

                        Words {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("The fans are not connected to the cooler")
                            size: 12.5
                            color: UI.Style.inkMuted
                        }
                    }
                }

                CurveChart {
                    id: chart
                    y: header.height + 10
                    // The board's chart stops 12px short of the card's padding.
                    width: parent.width - 12
                    points: page.shown === "fans" ? CoolerApp.fanCurve : CoolerApp.pumpCurve
                    nowTemperature: CoolerApp.liquidTemperature
                    nowText: page.shown === "fans" ? qsTr("Now: %1, fans at %2") : qsTr("Now: %1, pump at %2")
                    accessibleName: page.shown === "fans" ? qsTr("Fan speed") : qsTr("Pump speed")
                    onEdited: points => CoolerApp.setCurve(page.shown, points)
                }
            }
        }

        Column {
            id: side
            x: page.wide ? parent.width - width : 0
            y: page.wide ? 0 : chartCard.height + 14
            width: page.wide ? 300 : parent.width
            spacing: 12

            UI.Card {
                width: parent.width
                radius: UI.Style.radiusPanel
                clipContent: false
                padding: 1

                contentItem: Column {
                    OptionRow {
                        visible: page.fans
                        iconName: "silent"
                        title: qsTr("Stop the fans when cool")
                        description: qsTr("Below %1 liquid").arg(Format.degrees(CoolerApp.fanStopBelow))

                        UI.Toggle {
                            checked: CoolerApp.stopFansWhenCool
                            onToggled: CoolerApp.stopFansWhenCool = checked
                        }
                    }

                    OptionRow {
                        iconName: "gauge"
                        title: qsTr("Pump never below")

                        UI.ComboBox {
                            width: 116
                            height: 32
                            model: [40, 50, 60, 70, 80].map(p => ({ value: p, text: qsTr("%1 per cent").arg(p) }))
                            currentIndex: model.findIndex(o => o.value === CoolerApp.pumpMinimum)
                            onActivated: CoolerApp.pumpMinimum = currentValue
                        }
                    }

                    OptionRow {
                        iconName: "bolt"
                        title: qsTr("Full speed in games")
                        description: qsTr("When a game is running")

                        UI.Toggle {
                            checked: CoolerApp.fullSpeedInGames
                            onToggled: CoolerApp.fullSpeedInGames = checked
                        }
                    }
                }
            }

            UI.Card {
                width: parent.width
                radius: UI.Style.radiusPanel
                clipContent: false
                leftPadding: 17
                rightPadding: 17
                topPadding: 15
                bottomPadding: 15

                contentItem: Item {
                    implicitHeight: Math.max(18, safeText.height)

                    CoolerIcon {
                        name: "safe"
                        size: 18
                        color: Colours.good
                    }

                    Column {
                        id: safeText
                        x: 29
                        width: parent.width - x
                        spacing: 3

                        Words {
                            width: parent.width
                            text: qsTr("Safe without this app")
                            size: 13.5
                            font.weight: UI.Style.weightSemiBold
                        }

                        Words {
                            width: parent.width
                            text: qsTr("These settings are saved to the cooler, so they still apply before you sign in and if Cooler is closed.")
                            size: 12.5
                            line: 1.5
                            color: UI.Style.inkMuted
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
