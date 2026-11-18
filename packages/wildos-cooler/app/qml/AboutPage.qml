import QtQuick
import QtQuick.Dialogs
import WildOS.Cooler
import WildOS.UI as UI

// About this cooler (10.12): which cooler it is and whether it is
// answering, what is known about it, what Cooler can do with it on Wild OS,
// and the two things to do when something is wrong: put everything back,
// and save a report. What the page says about the screen, the fans and the
// lights is what this cooler reports, not what the board's example has.
Page {
    id: page

    title: qsTr("About this cooler")

    signal message(string text)

    readonly property bool wide: innerWidth >= 760

    // The cooler has one fan connector and tells whether anything on it is
    // turning, not how many fans share it, so no number is given.
    readonly property string summary: {
        if (CoolerApp.fansConnected && CoolerApp.hasLighting) {
            return qsTr("Liquid cooler with a screen, fans and lighting");
        }
        if (CoolerApp.fansConnected) {
            return qsTr("Liquid cooler with a screen and fans");
        }
        return CoolerApp.hasLighting ? qsTr("Liquid cooler with a screen and lighting") : qsTr("Liquid cooler with a screen");
    }
    readonly property string fansFact: CoolerApp.fansConnected ? qsTr("Plugged into the cooler") : qsTr("Not connected to the cooler")

    FileDialog {
        id: saveDialog
        title: qsTr("Save a report")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)")]
        onAccepted: page.message(CoolerApp.saveReport(selectedFile) ? qsTr("Report saved") : qsTr("The report could not be saved"))
    }

    UI.Card {
        width: parent.width
        radius: UI.Style.radiusPanel
        clipContent: false
        leftPadding: 21
        rightPadding: 21
        topPadding: 19
        bottomPadding: 19

        contentItem: Item {
            implicitHeight: 124

            Bezel {
                id: picture
                size: 124
                faceId: CoolerApp.face
                time: 1.5
            }

            Column {
                x: picture.width + 20
                width: checkAgain.x - 20 - x
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Words {
                    width: parent.width
                    text: CoolerApp.model
                    size: 20
                    font.weight: UI.Style.weightBold
                }

                Words {
                    width: parent.width
                    text: page.summary
                    color: UI.Style.inkMuted
                }

                Item {
                    width: parent.width
                    height: 4 + working.height

                    Row {
                        id: working
                        y: 4
                        spacing: 8

                        CoolerIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "working"
                            size: 16
                            strokeWidth: 2
                            color: Colours.good
                        }

                        Words {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Connected and working")
                            color: Colours.good
                        }
                    }
                }
            }

            UI.Button {
                id: checkAgain
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 32
                iconName: "refresh"
                text: qsTr("Check again")
                onClicked: CoolerApp.lookAgain()
            }
        }
    }

    Item {
        width: parent.width
        height: page.wide ? Math.max(cooler.height, works.height) : cooler.height + 14 + works.height

        Column {
            id: cooler
            width: page.wide ? (parent.width - 14) / 2 : parent.width
            spacing: 8

            Words {
                text: qsTr("The cooler")
                size: 14
                font.weight: UI.Style.weightSemiBold
                Accessible.role: Accessible.Heading
            }

            UI.Card {
                width: parent.width
                radius: UI.Style.radiusPanel
                clipContent: false
                padding: 1

                contentItem: Column {
                    InfoRow {
                        name: qsTr("Connection")
                        value: qsTr("USB, inside the PC")
                    }

                    InfoRow {
                        name: qsTr("Screen")
                        value: qsTr("Round, %1 by %2").arg(CoolerApp.screenSize).arg(CoolerApp.screenSize)
                    }

                    InfoRow {
                        visible: CoolerApp.firmware.length > 0
                        name: qsTr("Firmware")
                        value: qsTr("Version %1").arg(CoolerApp.firmware)
                    }

                    InfoRow {
                        name: qsTr("Fans")
                        value: page.fansFact
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
                        iconName: "back"
                        title: qsTr("Put everything back")
                        description: qsTr("Face, speeds and lighting return to how Cooler first set them")

                        UI.Button {
                            height: 32
                            text: qsTr("Reset")
                            Accessible.name: qsTr("Put everything back")
                            onClicked: CoolerApp.putEverythingBack()
                        }
                    }

                    OptionRow {
                        iconName: "report"
                        title: qsTr("Save a report")
                        description: qsTr("Readings and settings, to send with a problem report")

                        UI.Button {
                            height: 32
                            text: qsTr("Save")
                            Accessible.name: qsTr("Save a report")
                            onClicked: {
                                saveDialog.selectedFile = CoolerApp.reportFolder + "/cooler-report.txt";
                                saveDialog.open();
                            }
                        }
                    }
                }
            }
        }

        Column {
            id: works
            x: page.wide ? cooler.width + 14 : 0
            y: page.wide ? 0 : cooler.height + 14
            width: cooler.width
            spacing: 8

            Words {
                text: qsTr("What works on Wild OS")
                size: 14
                font.weight: UI.Style.weightSemiBold
                Accessible.role: Accessible.Heading
            }

            UI.Card {
                width: parent.width
                radius: UI.Style.radiusPanel
                clipContent: false
                padding: 1

                contentItem: Column {
                    Repeater {
                        model: [
                            { icon: "screen", title: qsTr("Screen faces"), note: qsTr("All thirteen, with live readings"), works: true },
                            { icon: "fan", title: qsTr("Pump and fan speeds"), note: qsTr("Modes and your own curve"), works: true },
                            { icon: "light", title: qsTr("Lighting"), note: qsTr("Every lit part in the PC"), works: true },
                            { icon: "thermometer", title: qsTr("Temperature readings"), note: qsTr("Liquid, pump and fan speeds"), works: true },
                            { icon: "download", title: qsTr("Firmware updates"), note: qsTr("Not from here. These need the maker's own software."), works: false }
                        ]

                        OptionRow {
                            id: feature
                            required property var modelData
                            required property int index
                            divider: index > 0
                            iconName: modelData.icon
                            title: modelData.title
                            description: modelData.note

                            CoolerIcon {
                                name: feature.modelData.works ? "check" : "none"
                                strokeWidth: 2.4
                                color: feature.modelData.works ? Colours.good : UI.Style.inkSubtle
                                Accessible.role: Accessible.StaticText
                                Accessible.name: feature.modelData.works ? qsTr("Works") : qsTr("Not available")
                            }
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
                    implicitHeight: Math.max(18, legal.height)

                    CoolerIcon {
                        name: "info"
                        size: 18
                    }

                    Words {
                        id: legal
                        x: 29
                        width: parent.width - x
                        // The first sentence is the one that matters, so it
                        // is in the full ink and weight 550.
                        text: qsTr("<b>Cooler works with NZXT Kraken coolers that have a screen.</b> Other makes are not supported yet. It is part of Wild OS, built on open-source drivers, and is not made or endorsed by NZXT. Kraken is a trademark of NZXT.")
                            .replace("<b>", "<span style=\"font-weight:%1; color:%2\">".arg(UI.Style.weightSemiBold).arg(UI.Style.ink))
                            .replace("</b>", "</span>")
                        textFormat: Text.RichText
                        size: 12.5
                        line: 1.55
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
