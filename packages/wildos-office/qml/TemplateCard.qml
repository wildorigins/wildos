import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A template on the start screen (WriteStart.dc): the first page of the
// template, 134 by 176 on white, with its name under it. The blank card
// is an empty page with the accent plus; a template the person saved
// themselves, with no picture, shows a page of plain lines.
T.AbstractButton {
    id: card

    property string name
    property string thumbnail
    property string description
    property bool blank: false
    readonly property bool lit: hovered || visualFocus

    implicitWidth: 150
    implicitHeight: column.implicitHeight + 16
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: name
    Accessible.description: description

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    background: Rectangle {
        radius: 10
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: card.lit
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: card.down ? UI.Style.surfacePressed : card.lit ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        UI.FocusRing {
            visible: card.visualFocus
        }
    }

    contentItem: Item {}

    Column {
        id: column
        x: 8
        y: 8
        width: card.width - 16
        spacing: 9

        Item {
            width: 134
            height: 176

            // The page's shadow: a soft edge a step below the paper.
            Rectangle {
                x: -1
                y: 1
                width: parent.width + 2
                height: parent.height + 2
                radius: 5
                color: UI.Style.dark ? Qt.rgba(0, 0, 0, 0.30) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.10)
            }
            Rectangle {
                x: -2
                y: 3
                width: parent.width + 4
                height: parent.height + 2
                radius: 6
                color: UI.Style.dark ? Qt.rgba(0, 0, 0, 0.14) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.05)
            }
            Rectangle {
                id: page
                anchors.fill: parent
                radius: 4
                color: "#ffffff"
                border.width: UI.Style.dark ? 0 : 1
                border.color: Qt.rgba(46 / 255, 48 / 255, 52 / 255, 0.10)
                clip: true

                Image {
                    visible: card.thumbnail.length > 0
                    anchors.fill: parent
                    anchors.margins: 1
                    source: card.thumbnail
                    sourceSize.width: 268
                    sourceSize.height: 352
                    fillMode: Image.PreserveAspectCrop
                    verticalAlignment: Image.AlignTop
                    smooth: true
                    mipmap: true
                    asynchronous: true
                }

                UI.Icon {
                    visible: card.blank
                    anchors.centerIn: parent
                    name: "plus"
                    size: 30
                    strokeWidth: 2
                    color: "#4560d8"
                }

                Column {
                    visible: !card.blank && card.thumbnail.length === 0
                    x: 16
                    y: 18
                    spacing: 5
                    Rectangle {
                        width: 60
                        height: 6
                        radius: 2
                        color: "#1f2633"
                    }
                    Item {
                        width: 1
                        height: 4
                    }
                    Repeater {
                        model: [0.95, 0.88, 0.92, 0.8, 0.9, 0.6]
                        Rectangle {
                            required property real modelData
                            width: 102 * modelData
                            height: 3
                            radius: 2
                            color: "#d7dbe3"
                        }
                    }
                }
            }
        }

        UI.Label {
            width: parent.width
            text: card.name
            size: 13
            font.weight: UI.Style.weightMedium
        }
        UI.Label {
            visible: card.description.length > 0
            width: parent.width
            text: card.description
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            lineHeight: 1.25
            verticalAlignment: Text.AlignTop
        }
    }
}
