import QtQuick
import org.kde.plasma.networkmanagement as PlasmaNM

// What the computer is connected to, in one line, from the same models the
// network tray icon uses.
QtObject {
    id: summary

    property string name: ""
    property int type: PlasmaNM.Enums.UnknownConnectionType
    property bool connecting: false
    readonly property bool connected: name.length > 0
    readonly property bool wifi: type === PlasmaNM.Enums.Wireless
    readonly property bool online: status.connectivity === 4
    readonly property string title: connected ? name : connecting ? qsTr("Connecting") : qsTr("Not connected")
    readonly property string caption: connected
        ? (online ? (wifi ? qsTr("Wi-Fi, connected") : qsTr("Connected")) : qsTr("Connected, no internet"))
        : (enabledConnections.wirelessEnabled ? qsTr("No network in use") : qsTr("Wi-Fi is off"))
    readonly property string iconName: connected ? (wifi ? "wifi" : "ethernet") : "wifi-off"

    property PlasmaNM.NetworkStatus status: PlasmaNM.NetworkStatus {}
    property PlasmaNM.EnabledConnections enabledConnections: PlasmaNM.EnabledConnections {}
    property PlasmaNM.NetworkModel networkModel: PlasmaNM.NetworkModel {}

    property Instantiator watcher: Instantiator {
        model: summary.networkModel
        delegate: QtObject {
            readonly property string itemName: model.ItemUniqueName
            readonly property int state: model.ConnectionState
            readonly property int itemType: model.Type
            readonly property bool active: state === PlasmaNM.Enums.Activated
                && (itemType === PlasmaNM.Enums.Wireless || itemType === PlasmaNM.Enums.Wired)
            onActiveChanged: summary.refresh()
            onStateChanged: summary.refresh()
            Component.onCompleted: summary.refresh()
            Component.onDestruction: Qt.callLater(summary.refresh)
        }
    }

    function refresh() {
        let found = "";
        let foundType = PlasmaNM.Enums.UnknownConnectionType;
        let busy = false;
        for (let i = 0; i < watcher.count; ++i) {
            const item = watcher.objectAt(i);
            if (!item) {
                continue;
            }
            if (item.active && found.length === 0) {
                found = item.itemName;
                foundType = item.itemType;
            }
            if (item.state === PlasmaNM.Enums.Activating) {
                busy = true;
            }
        }
        // The model can lag behind or leave out a wired connection set up
        // outside Plasma (the one made at install time). NetworkManager's own
        // connectivity still says whether the computer is online, so the card
        // never claims "Not connected" while it is.
        if (found.length === 0 && status.connectivity >= 2) {
            const active = (status.activeConnections || "").split("\n")[0].replace(/\s*\(.*\)\s*$/, "").replace(/:.*$/, "").trim();
            found = active.length > 0 ? active : qsTr("Wired connection");
            foundType = PlasmaNM.Enums.Wired;
        }
        name = found;
        type = foundType;
        connecting = busy;
    }

    property Connections statusWatcher: Connections {
        target: summary.status
        function onConnectivityChanged() { summary.refresh(); }
        function onActiveConnectionsChanged() { summary.refresh(); }
    }
}
