import QtQuick
import org.kde.bluezqt as BluezQt
import org.kde.plasma.private.bluetooth as PlasmaBt

// Bluetooth state for Home: whether there is an adapter, whether it is on,
// and the devices this computer knows. Switching it uses rfkill the way the
// tray icon does.
QtObject {
    id: summary

    readonly property var manager: BluezQt.Manager
    readonly property bool present: manager.adapters.length > 0 || manager.bluetoothBlocked
    readonly property bool on: manager.bluetoothOperational
    property int connectedCount: 0
    // Paired or connected devices, connected ones first. BlueZ keeps no
    // record of when a device last connected, so among the rest the order
    // is by name.
    property var devices: []

    function recount() {
        const known = [];
        let n = 0;
        for (const device of manager.devices) {
            if (device.connected) {
                ++n;
            }
            if (device.paired || device.connected) {
                known.push(device);
            }
        }
        known.sort((a, b) => (b.connected - a.connected) || a.name.localeCompare(b.name));
        connectedCount = n;
        devices = known;
    }

    function setOn(enabled) {
        manager.bluetoothBlocked = !enabled;
        for (const adapter of manager.adapters) {
            adapter.powered = enabled;
        }
    }

    function addDevice() {
        PlasmaBt.LaunchApp.launchWizard();
    }

    // BluezQt device types: 2 computer, 3 phone, 4 to 6 audio, 7 keyboard,
    // 8 mouse, as on the Bluetooth page.
    function iconFor(type) {
        if (type >= 4 && type <= 6) {
            return "headphones";
        }
        return type === 2 ? "system" : type === 3 ? "phone" : type === 7 ? "keyboard" : type === 8 ? "mouse" : "bluetooth";
    }

    function stateOf(device) {
        if (!device.connected) {
            return qsTr("Not connected");
        }
        return device.battery ? qsTr("Connected · %1%").arg(device.battery.percentage) : qsTr("Connected");
    }

    readonly property string caption: !present ? qsTr("Not available")
        : !on ? qsTr("Off")
        : devices.length === 0 ? qsTr("On, no devices yet")
        : devices.length === 1 ? qsTr("1 device, %1 connected").arg(connectedCount)
        : qsTr("%1 devices, %2 connected").arg(devices.length).arg(connectedCount)

    property Connections watch: Connections {
        target: summary.manager
        function onDeviceChanged() { summary.recount(); }
        function onDeviceAdded() { summary.recount(); }
        function onDeviceRemoved() { summary.recount(); }
        function onBluetoothOperationalChanged() { summary.recount(); }
    }

    Component.onCompleted: recount()
}
