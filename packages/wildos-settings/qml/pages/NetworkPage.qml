import QtQuick
import QtQuick.Layouts
import org.kde.plasma.networkmanagement as PlasmaNM
import WildOS.Settings
import WildOS.UI as UI

// Network and internet: Wi-Fi on or off, flight mode, nearby networks with
// connecting and password entry, and Ethernet. Everything goes through
// plasma-nm, the same code as the network icon in the tray; the rarer
// settings (VPN, proxy, IP addresses) are on Advanced network settings.
PageScroll {
    id: page

    property string expanded: ""

    NetworkSummary {
        id: summary
    }

    PlasmaNM.Handler {
        id: handler
    }

    PlasmaNM.AvailableDevices {
        id: devices
    }

    PlasmaNM.NetworkModel {
        id: networkModel
    }

    PlasmaNM.AppletProxyModel {
        id: proxy
        sourceModel: networkModel
    }

    Timer {
        interval: 10000
        repeat: true
        triggeredOnStart: true
        running: summary.enabledConnections.wirelessEnabled && !PlasmaNM.Configuration.airplaneModeEnabled
        onTriggered: handler.requestScan()
    }

    StatusCard {
        iconName: summary.iconName
        warning: summary.connected && !summary.online
        title: summary.connected ? "Connected to " + summary.name : summary.title
        description: summary.connected
            ? (summary.online ? "You are online." : "This network is not giving internet access. You might need to sign in to it in your browser.")
            : PlasmaNM.Configuration.airplaneModeEnabled ? "Flight mode is on, so wireless connections are off."
            : summary.enabledConnections.wirelessEnabled ? "Pick a network below to connect."
            : "Turn on Wi-Fi to see networks nearby."
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                visible: devices.wirelessDeviceAvailable
                iconName: "wifi"
                title: "Wi-Fi"
                description: "Connect to wireless networks"
                status: (summary.enabledConnections.wirelessEnabled) ? "On" : "Off"

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: summary.enabledConnections.wirelessEnabled
                    Accessible.name: "Wi-Fi"
                    onToggled: handler.enableWireless(checked)
                }
            }

            UI.SettingRow {
                iconName: "airplane"
                title: "Flight mode"
                description: "Turn off every wireless connection, including Bluetooth"
                status: (PlasmaNM.Configuration.airplaneModeEnabled) ? "On" : "Off"

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: PlasmaNM.Configuration.airplaneModeEnabled
                    Accessible.name: "Flight mode"
                    onToggled: {
                        handler.enableAirplaneMode(checked);
                        PlasmaNM.Configuration.airplaneModeEnabled = checked;
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: devices.wirelessDeviceAvailable && summary.enabledConnections.wirelessEnabled
        spacing: 10

        SectionTitle {
            text: "Wi-Fi networks"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: proxy

                    UI.SettingRow {
                        id: net
                        required property var model
                        readonly property bool wireless: model.Type === PlasmaNM.Enums.Wireless
                        readonly property bool active: model.ConnectionState === PlasmaNM.Enums.Activated
                        readonly property bool activating: model.ConnectionState === PlasmaNM.Enums.Activating
                        readonly property bool secured: model.SecurityType > PlasmaNM.Enums.NoneSecurity
                        // A saved network knows its password already; a new
                        // secured one needs it typed in here first.
                        readonly property bool needsPassword: secured && model.Uuid.length === 0
                            && model.SecurityType !== PlasmaNM.Enums.WpaEap && model.SecurityType !== PlasmaNM.Enums.Wpa2Eap
                        readonly property bool showPassword: page.expanded === model.ItemUniqueName

                        visible: wireless
                        height: visible ? implicitHeight : 0
                        implicitHeight: showPassword ? 62 + 56 : 62
                        iconName: model.Signal > 66 ? "wifi" : model.Signal > 33 ? "wifi-mid" : "wifi-low"
                        title: model.ItemUniqueName
                        description: active ? (secured ? "Connected, secured" : "Connected")
                            : activating ? "Connecting"
                            : secured ? (model.Uuid.length > 0 ? "Secured, saved" : "Secured") : "Open network"
                        bottomPadding: showPassword ? 56 : 0

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: net.active ? "Disconnect" : net.activating ? "Connecting" : "Connect"
                            enabled: !net.activating
                            kind: !net.active && net.showPassword ? "primary" : "secondary"
                            visible: !net.showPassword
                            onClicked: {
                                if (net.active) {
                                    handler.deactivateConnection(net.model.ConnectionPath, net.model.DevicePath);
                                } else if (net.model.Uuid.length > 0) {
                                    handler.activateConnection(net.model.ConnectionPath, net.model.DevicePath, net.model.SpecificPath);
                                } else if (net.needsPassword) {
                                    page.expanded = net.model.ItemUniqueName;
                                    password.text = "";
                                    password.forceActiveFocus();
                                } else {
                                    handler.addAndActivateConnection(net.model.DevicePath, net.model.SpecificPath);
                                }
                            }
                        }

                        RowLayout {
                            visible: net.showPassword
                            parent: net
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 18 + 20 + 16
                            anchors.rightMargin: 18
                            anchors.bottomMargin: 14
                            spacing: 8

                            UI.TextField {
                                id: password
                                Layout.fillWidth: true
                                placeholderText: "Network password"
                                revealable: true
                                UI.NoPaste {}
                                onAccepted: connectButton.clicked()
                                Keys.onEscapePressed: page.expanded = ""
                            }

                            UI.Button {
                                id: connectButton
                                text: "Connect"
                                kind: "primary"
                                enabled: password.text.length >= 8 || net.model.SecurityType === PlasmaNM.Enums.StaticWep
                                onClicked: {
                                    handler.addAndActivateConnection(net.model.DevicePath, net.model.SpecificPath, password.text);
                                    page.expanded = "";
                                }
                            }

                            UI.Button {
                                text: "Cancel"
                                onClicked: page.expanded = ""
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                visible: devices.wiredDeviceAvailable
                iconName: "ethernet"
                title: "Ethernet"
                description: summary.connected && !summary.wifi ? "Connected. Addresses and other details are in the advanced settings." : "Not connected. Plug in a network cable to use it."
                chevron: true
                onClicked: Navigator.open("networkadvanced")
            }

            UI.SettingRow {
                iconName: "globe"
                title: "Advanced network settings"
                description: "VPN, proxy, IP addresses and saved networks"
                chevron: true
                onClicked: Navigator.open("networkadvanced")
            }
        }
    }
}
