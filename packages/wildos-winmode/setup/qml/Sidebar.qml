import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The left column of the setup sheet (WinModeCheck.dc to WinModeReady.dc):
// 260 wide on the sunken 0.28 band, the 30px tile and "Windows mode" at
// 17px, then the four steps as 40px rows, the current one on 0.09 with an
// accent number, those done ticked.
Item {
    id: sidebar

    property int current: 1
    readonly property var steps: [qsTr("Check this PC"), qsTr("Choose options"), qsTr("Set up"), qsTr("Ready")]

    implicitWidth: 260

    Item {
        anchors.fill: parent
        clip: true

        Rectangle {
            width: parent.width + UI.Style.radiusXl
            height: parent.height
            radius: UI.Style.radiusXl
            color: UI.Style.surfaceSunken
        }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: UI.Style.borderSubtle
    }

    // Dragging the band moves the window: the sheet has no title bar.
    MouseArea {
        anchors.fill: parent
        onPressed: sidebar.Window.window.startSystemMove()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.topMargin: 32
        anchors.bottomMargin: 26
        spacing: 28

        Row {
            spacing: 12

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                sourceSize: Qt.size(60, 60)
                source: "qrc:/qt/qml/WildOS/WinMode/images/wild-os-icon.svg"
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Windows mode")
                size: 17
                font.weight: UI.Style.weightSemiBold
                Accessible.role: Accessible.Heading
            }
        }

        Column {
            Layout.fillWidth: true
            spacing: 4
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Steps")

            Repeater {
                model: sidebar.steps

                Rectangle {
                    id: row

                    required property int index
                    required property string modelData
                    readonly property int number: index + 1
                    readonly property bool done: number < sidebar.current
                    readonly property bool active: number === sidebar.current

                    width: parent.width
                    height: 40
                    radius: UI.Style.radiusSm
                    color: active ? UI.Style.surfaceHover : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: done ? qsTr("%1, done").arg(modelData) : active ? qsTr("%1, current step").arg(modelData) : modelData

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12

                        Rectangle {
                            width: 22
                            height: 22
                            radius: 11
                            anchors.verticalCenter: parent.verticalCenter
                            color: row.done ? UI.Style.accentAlpha(0.25, 0.19) : row.active ? UI.Style.accent : "transparent"
                            border.width: row.done || row.active ? 0 : 1.5
                            border.color: UI.Style.inkAlpha(0.45, 0.40)

                            UI.Icon {
                                anchors.centerIn: parent
                                visible: row.done
                                name: "checkmark"
                                size: 12
                                strokeWidth: 3
                                color: UI.Style.accentText
                            }

                            UI.Label {
                                anchors.centerIn: parent
                                visible: !row.done
                                text: row.number
                                size: 11
                                font.weight: row.active ? UI.Style.weightBold : UI.Style.weightRegular
                                color: row.active ? UI.Style.textOnAccent : UI.Style.ink
                            }
                        }

                        UI.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData
                            size: 13
                            font.weight: row.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                            color: row.active ? UI.Style.ink : UI.Style.inkAlpha(row.done ? 0.80 : 0.75, 0.70)
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
