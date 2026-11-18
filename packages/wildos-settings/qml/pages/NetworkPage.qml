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
        title: summary.connected ? qsTr("Connected to %1").arg(summary.name) : summary.title
        description: summary.connected
            ? (summary.online ? qsTr("You are online.") : qsTr("This network is not giving internet access. You might need to sign in to it in your browser."))
            : PlasmaNM.Configuration.airplaneModeEnabled ? qsTr("Flight mode is on, so wireless connections are off.")
            : summary.enabledConnections.wirelessEnabled ? qsTr("Pick a network below to connect.")
            : qsTr("Turn on Wi-Fi to see networks nearby.")
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                visible: devices.wirelessDeviceAvailable
                iconName: "wifi"
                title: "Wi-Fi"
                description: qsTr("Connect to wireless networks")
                status: (summary.enabledConnections.wirelessEnabled) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: summary.enabledConnections.wirelessEnabled
                    Accessible.name: "Wi-Fi"
                    onToggled: handler.enableWireless(checked)
                }
            }

            UI.SettingRow {
                iconName: "airplane"
                title: qsTr("Flight mode")
                description: qsTr("Turn off every wireless connection, including Bluetooth")
                status: (PlasmaNM.Configuration.airplaneModeEnabled) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: PlasmaNM.Configuration.airplaneModeEnabled
                    Accessible.name: qsTr("Flight mode")
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
            text: qsTr("Wi-Fi networks")
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
                        description: active ? (secured ? qsTr("Connected, secured") : qsTr("Connected"))
                            : activating ? qsTr("Connecting")
                            : secured ? (model.Uuid.length > 0 ? qsTr("Secured, saved") : qsTr("Secured")) : qsTr("Open network")
                        bottomPadding: showPassword ? 56 : 0

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: net.active ? qsTr("Disconnect") : net.activating ? qsTr("Connecting") : qsTr("Connect")
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
                                placeholderText: qsTr("Network password")
                                revealable: true
                                UI.NoPaste {}
                                onAccepted: connectButton.clicked()
                                Keys.onEscapePressed: page.expanded = ""
                            }

                            UI.Button {
                                id: connectButton
                                text: qsTr("Connect")
                                kind: "primary"
                                enabled: password.text.length >= 8 || net.model.SecurityType === PlasmaNM.Enums.StaticWep
                                onClicked: {
                                    handler.addAndActivateConnection(net.model.DevicePath, net.model.SpecificPath, password.text);
                                    page.expanded = "";
                                }
                            }

                            UI.Button {
                                text: qsTr("Cancel")
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
                description: summary.connected && !summary.wifi ? qsTr("Connected. Addresses and other details are in the advanced settings.") : qsTr("Not connected. Plug in a network cable to use it.")
                chevron: true
                onClicked: Navigator.open("networkadvanced")
            }

            UI.SettingRow {
                iconName: "globe"
                title: qsTr("Advanced network settings")
                description: qsTr("VPN, proxy, IP addresses and saved networks")
                chevron: true
                onClicked: Navigator.open("networkadvanced")
            }
        }
    }
}
