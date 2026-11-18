import QtQuick
import QtQuick.Layouts
import org.kde.bluezqt as BluezQt
import org.kde.plasma.private.bluetooth as PlasmaBt
import WildOS.Settings
import WildOS.UI as UI

// Bluetooth, after Windows 11: the switch, a way to add a device, and the
// devices this computer knows with connect and remove. Everything goes
// through BluezQt and bluedevil, as the taskbar's Bluetooth does.
PageScroll {
    id: page

    readonly property var manager: BluezQt.Manager
    readonly property bool present: manager.adapters.length > 0 || manager.bluetoothBlocked
    readonly property bool on: manager.bluetoothOperational
    readonly property var adapter: manager.usableAdapter || (manager.adapters.length > 0 ? manager.adapters[0] : null)

    function setOn(enabled) {
        manager.bluetoothBlocked = !enabled;
        for (const a of manager.adapters) {
            a.powered = enabled;
        }
    }

    // BluezQt device types: 2 computer, 3 phone, 4 to 6 audio, 7 keyboard,
    // 8 mouse. Everything else gets the Bluetooth mark.
    function iconFor(type) {
        if (type >= 4 && type <= 6) {
            return "headphones";
        }
        return type === 2 ? "system" : type === 3 ? "phone" : type === 7 ? "keyboard" : type === 8 ? "mouse" : "bluetooth";
    }

    PlasmaBt.DevicesProxyModel {
        id: devices
        hideBlockedDevices: true
        // Set once rather than bound: the shared model re-announces itself,
        // which a binding reports as a loop.
        Component.onCompleted: sourceModel = PlasmaBt.SharedDevicesStateProxyModel
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "bluetooth"
                title: "Bluetooth"
                description: !page.present ? qsTr("This computer has no Bluetooth")
                    : page.on && page.adapter ? qsTr("Discoverable as “%1”").arg(page.adapter.name)
                    : qsTr("Turn on to use headphones, a mouse or a keyboard")
                enabled: page.present
                status: (page.on) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.on
                    Accessible.name: "Bluetooth"
                    onToggled: page.setOn(checked)
                }
            }

            UI.SettingRow {
                visible: page.on
                iconName: "plus"
                title: qsTr("Add a device")
                description: qsTr("Put the device in pairing mode first")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Add device")
                    onClicked: PlasmaBt.LaunchApp.launchWizard()
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.on
        spacing: 10

        SectionTitle {
            text: qsTr("Devices")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    id: deviceList
                    model: devices

                    UI.SettingRow {
                        id: deviceRow
                        required property var model
                        iconName: page.iconFor(model.Type)
                        title: model.Name
                        description: {
                            if (model.Connecting) {
                                return model.Connected ? qsTr("Disconnecting") : qsTr("Connecting");
                            }
                            if (model.Connected) {
                                return model.Battery ? qsTr("Connected, battery %1%").arg(model.Battery.percentage) : qsTr("Connected");
                            }
                            if (model.ConnectionFailed) {
                                return qsTr("Could not connect");
                            }
                            return model.Paired ? qsTr("Paired") : qsTr("Not paired");
                        }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            enabled: !deviceRow.model.Connecting
                            text: deviceRow.model.Connected ? qsTr("Disconnect") : qsTr("Connect")
                            onClicked: {
                                const device = deviceRow.model.Device;
                                const call = deviceRow.model.Connected ? device.disconnectFromDevice() : device.connectToDevice();
                                PlasmaBt.SharedDevicesStateProxyModel.registerPendingCallForDeviceUbi(call, deviceRow.model.Ubi);
                            }
                        }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Remove")
                            onClicked: {
                                const device = deviceRow.model.Device;
                                if (device && device.adapter) {
                                    device.adapter.removeDevice(device);
                                }
                            }
                        }
                    }
                }

                UI.SettingRow {
                    visible: deviceList.count === 0
                    title: qsTr("No devices yet")
                    description: qsTr("Devices you pair appear here.")
                }
            }
        }
    }
}
