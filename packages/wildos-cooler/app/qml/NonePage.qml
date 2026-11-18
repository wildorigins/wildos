import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// What the window shows when no supported cooler is found (10.13): one
// message in the middle, the two likely reasons, and Look again.
Flickable {
    id: page

    signal closeRequested()

    contentWidth: width
    contentHeight: Math.max(height, column.height + 48)
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    T.ScrollBar.vertical: UI.ScrollBar {}

    Column {
        id: column
        width: Math.min(560, page.width - 48)
        anchors.centerIn: parent
        spacing: 16

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 86
            height: 86
            radius: 43
            color: UI.Style.surfaceRaised
            border.width: 1
            border.color: UI.Style.borderSubtle

            CoolerIcon {
                anchors.centerIn: parent
                name: "not-found"
                size: 34
                strokeWidth: 1.6
            }
        }

        Words {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No supported cooler found")
            size: 24
            font.weight: UI.Style.weightBold
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
        }

        Words {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Cooler works with NZXT Kraken coolers that have a screen. It does not control other makes of cooler yet.")
            size: 14
            line: 1.55
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }

        UI.Card {
            width: parent.width
            radius: UI.Style.radiusPanel
            clipContent: false
            padding: 1

            contentItem: Column {
                Repeater {
                    model: [
                        { icon: "usb", title: qsTr("Have a Kraken?"), note: qsTr("Check its USB lead is plugged into the motherboard, then look again.") },
                        { icon: "fan", title: qsTr("A different cooler?"), note: qsTr("Its fans still follow the motherboard as normal. There is nothing to set up here.") }
                    ]

                    Item {
                        id: reason
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: (index > 0 ? 1 : 0) + 14 + words.height + 14

                        Rectangle {
                            visible: reason.index > 0
                            width: parent.width
                            height: 1
                            color: UI.Style.borderSubtle
                        }

                        CoolerIcon {
                            x: 16
                            y: parent.height - 14 - words.height
                            name: reason.modelData.icon
                            size: 18
                        }

                        Column {
                            id: words
                            x: 47
                            y: parent.height - 14 - height
                            width: parent.width - x - 16
                            spacing: 2

                            Words {
                                width: parent.width
                                text: reason.modelData.title
                                size: 13.5
                                font.weight: UI.Style.weightSemiBold
                            }

                            Words {
                                width: parent.width
                                text: reason.modelData.note
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

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            UI.Button {
                height: 32
                kind: "primary"
                iconName: "refresh"
                text: qsTr("Look again")
                onClicked: CoolerApp.lookAgain()
            }

            UI.Button {
                height: 32
                text: qsTr("Close")
                onClicked: page.closeRequested()
            }
        }
    }
}
