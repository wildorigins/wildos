import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// One card of System > Devices (SettingsDevices.dc): a row per device with
// its plain name and one line, the button that fixes it when it needs you,
// and its state at the right. Rows are Devices' maps.
Rectangle {
    id: card

    property var model: []
    // The board's status colours, as History and Firewall use them.
    readonly property color amber: UI.Style.dark ? "#ffc46a" : UI.Style.warning
    readonly property color green: UI.Style.dark ? "#5fd38d" : "#1d7a4c"

    signal opened(var row)
    signal acted(var row)

    implicitHeight: rowsColumn.implicitHeight
    radius: 12
    color: UI.Style.tone(0.05)
    border.width: 1
    border.color: UI.Style.tone(0.07)
    clip: true

    Column {
        id: rowsColumn
        width: parent.width

        Repeater {
            model: card.model

            T.AbstractButton {
                id: deviceRow
                required property var modelData
                required property int index
                readonly property bool needsYou: modelData.state === "attention"
                readonly property bool off: modelData.state === "off"
                width: rowsColumn.width
                height: 50
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name + ". " + (needsYou ? modelData.problem : modelData.line)
                Accessible.description: "Details"
                onClicked: card.opened(modelData)

                background: Rectangle {
                    color: deviceRow.hovered ? UI.Style.tone(0.03) : "transparent"
                    radius: 12

                    Rectangle {
                        visible: deviceRow.index > 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: UI.Style.tone(0.07)
                    }

                    UI.FocusRing {
                        visible: deviceRow.visualFocus
                        targetRadius: 12
                    }
                }

                contentItem: RowLayout {
                    spacing: 14

                    PathIcon {
                        Layout.leftMargin: 18
                        name: deviceRow.modelData.icon
                        size: 19
                        strokeWidth: 1.8
                        color: UI.Style.iconInk
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        UI.Label {
                            Layout.fillWidth: true
                            text: deviceRow.modelData.name
                            size: 13.5
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: deviceRow.needsYou ? deviceRow.modelData.problem : deviceRow.modelData.line
                            size: 12
                            color: UI.Style.inkMuted
                        }
                    }

                    UI.Button {
                        visible: deviceRow.needsYou && deviceRow.modelData.action !== ""
                        implicitHeight: 32
                        text: deviceRow.modelData.actionLabel
                        Accessible.name: deviceRow.modelData.actionLabel + ": " + deviceRow.modelData.name
                        onClicked: card.acted(deviceRow.modelData)
                    }

                    Item {
                        visible: deviceRow.needsYou && deviceRow.modelData.action !== ""
                        Layout.preferredWidth: 12
                    }

                    RowLayout {
                        Layout.rightMargin: 18
                        spacing: 6

                        PathIcon {
                            name: deviceRow.needsYou ? "triangle-alert" : deviceRow.off ? "power-round" : "status-ok"
                            size: 14
                            strokeWidth: 2.2
                            color: deviceRow.needsYou ? card.amber : deviceRow.off ? UI.Style.inkMuted : card.green
                        }

                        UI.Label {
                            text: deviceRow.needsYou || deviceRow.off ? deviceRow.modelData.badge : "Working"
                            size: 12.5
                            color: deviceRow.needsYou ? card.amber : UI.Style.inkMuted
                        }
                    }
                }
            }
        }
    }
}
