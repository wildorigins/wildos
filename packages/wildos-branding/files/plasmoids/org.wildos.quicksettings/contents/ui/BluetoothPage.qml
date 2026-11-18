pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// Bluetooth devices this computer knows. Choosing one connects or
// disconnects it. "Add a device" looks for devices in pairing mode for half
// a minute and lists them here; choosing a new one pairs and connects it,
// with any code to confirm asked for by the system's Bluetooth agent.
FocusScope {
    id: page

    required property var quick
    readonly property var bluetooth: quick.bluetooth

    signal back()

    function focusFirst(): void {
        header.backButton.forceActiveFocus(Qt.TabFocusReason);
    }

    // BluezQt device types: 2 computer, 4 to 6 audio, 7 keyboard, 8 mouse.
    // Everything else gets a plain device outline.
    function iconFor(type: int): string {
        if (type >= 4 && type <= 6) {
            return Icons.headphones;
        }
        return type === 2 ? Icons.computer : type === 7 ? Icons.keyboard : type === 8 ? Icons.mouse : Icons.device;
    }

    readonly property bool searching: bluetooth !== null && bluetooth.discovering

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        PageHeader {
            id: header
            Layout.fillWidth: true
            title: "Bluetooth"
            hasSwitch: true
            switchOn: page.bluetooth !== null && page.bluetooth.enabled
            onBack: page.back()
            onSwitchToggled: page.bluetooth.setEnabled(!page.bluetooth.enabled)
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: -4
                leftMargin: 4
                rightMargin: 4
                topMargin: 4
                bottomMargin: 4
                clip: true
                spacing: 4
                visible: page.bluetooth !== null && page.bluetooth.enabled && count > 0
                model: page.bluetooth ? page.bluetooth.devices : null
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: UI.ScrollBar {}

                delegate: ListRow {
                    id: row

                    required property var model

                    width: ListView.view.width - 8
                    title: model.Name
                    active: model.Connected
                    subtitle: {
                        if (model.Connecting) {
                            return model.Connected ? qsTr("Disconnecting") : qsTr("Connecting");
                        }
                        if (model.Connected) {
                            return model.Battery ? qsTr("Connected, battery %1%").arg(model.Battery.percentage) : qsTr("Connected");
                        }
                        if (model.ConnectionFailed) {
                            return qsTr("Could not connect");
                        }
                        return model.Paired ? qsTr("Paired") : qsTr("Not paired, choose to pair");
                    }
                    icon: Component {
                        UI.Glyph {
                            path: page.iconFor(row.model.Type)
                            color: row.active ? UI.Style.accent : UI.Style.iconInk
                            strokeWidth: 1.9
                        }
                    }

                    onClicked: {
                        if (!model.Connecting) {
                            page.bluetooth.toggleDevice(model.Device, model.Ubi, model.Connected, model.Paired);
                        }
                    }
                }
            }

            Placeholder {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 320)
                visible: !list.visible
                iconPath: Icons.bluetooth
                heading: page.bluetooth === null || !page.bluetooth.available ? qsTr("Bluetooth is not available")
                    : !page.bluetooth.enabled ? qsTr("Bluetooth is off")
                    : page.searching ? qsTr("Looking for devices") : qsTr("No devices yet")
                explanation: page.bluetooth === null || !page.bluetooth.available ? qsTr("This computer has no Bluetooth adapter.")
                    : !page.bluetooth.enabled ? qsTr("Turn it on to use your headphones, mouse or keyboard.")
                    : page.searching ? qsTr("Devices in pairing mode nearby will appear here.")
                    : qsTr("Put your device in pairing mode, then add it here.")
                actionText: page.bluetooth === null || !page.bluetooth.available ? ""
                    : !page.bluetooth.enabled ? qsTr("Turn on Bluetooth") : qsTr("Add a device")
                busy: page.searching
                onAction: page.bluetooth.enabled ? page.bluetooth.addDevice() : page.bluetooth.setEnabled(true)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            UI.LinkButton {
                Layout.leftMargin: -4
                text: qsTr("More Bluetooth settings")
                onClicked: page.quick.openSettings("bluetooth")
            }

            Item {
                Layout.fillWidth: true
            }

            UI.LinkButton {
                visible: list.visible
                enabled: !page.searching
                Layout.rightMargin: -4
                text: page.searching ? qsTr("Looking for devices") : qsTr("Add a device")
                onClicked: page.bluetooth.addDevice()
            }
        }
    }
}
