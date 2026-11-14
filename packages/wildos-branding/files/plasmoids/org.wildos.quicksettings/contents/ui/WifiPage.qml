pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI
import org.kde.plasma.networkmanagement as PlasmaNM
import org.kde.plasma.plasma5support as P5Support

// Wi-Fi networks, opened from the Wi-Fi tile's chevron. The design has no
// other Wi-Fi screen, so this is where people choose and join a network.
FocusScope {
    id: page

    required property var quick
    readonly property var network: quick.network

    property int openRow: -1
    property string failure: ""
    // "Join a hidden network": a network that does not announce its name.
    property bool joiningHidden: false
    property bool joinBusy: false

    // Quoted for the shell: anything typed stays one argument.
    function quoted(text: string): string {
        return "'" + text.replace(/'/g, "'\\''") + "'";
    }

    function joinHidden(): void {
        joinBusy = true;
        failure = "";
        // NetworkManager probes for the name itself when told it is hidden.
        const password = hiddenPassword.text.length > 0 ? " password " + quoted(hiddenPassword.text) : "";
        joiner.connectSource("nmcli device wifi connect " + quoted(hiddenName.text) + password + " hidden yes");
    }

    P5Support.DataSource {
        id: joiner
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source);
            page.joinBusy = false;
            if (data["exit code"] === 0) {
                page.joiningHidden = false;
                hiddenName.text = "";
                hiddenPassword.text = "";
            } else {
                page.failure = "Couldn't join that network. Check its name and password, and that it's in range.";
                failureTimer.restart();
            }
        }
    }

    signal back()

    function focusFirst(): void {
        header.backButton.forceActiveFocus(Qt.TabFocusReason);
    }

    // NetworkManager only lists what it has seen lately, so look again every
    // ten seconds while the page is open, as the stock applet does.
    Timer {
        interval: 10000
        repeat: true
        triggeredOnStart: true
        running: page.visible && page.network !== null && page.network.wifiEnabled
        onTriggered: page.network.requestScan()
    }

    Timer {
        id: failureTimer
        interval: 8000
        onTriggered: page.failure = ""
    }

    Connections {
        target: page.network
        function onActivationFailed(message) {
            // NetworkManager's own wording is technical, so say it plainly.
            page.failure = "That did not work. Check the password and try again.";
            failureTimer.restart();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        PageHeader {
            id: header
            Layout.fillWidth: true
            title: "Wi-Fi"
            hasSwitch: true
            switchOn: page.network !== null && page.network.wifiEnabled
            onBack: page.back()
            onSwitchToggled: page.network.setWifiEnabled(!page.network.wifiEnabled)
        }

        Banner {
            Layout.fillWidth: true
            visible: page.network !== null && page.network.portal
            text: "This network needs you to sign in."
            actionText: "Open sign-in page"
            onAction: {
                Qt.openUrlExternally(page.network.networkCheckUrl);
                page.quick.expanded = false;
            }
        }

        Banner {
            Layout.fillWidth: true
            visible: page.failure.length > 0
            warning: true
            text: page.failure
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                // Room for focus rings drawn outside the rows.
                anchors.margins: -4
                leftMargin: 4
                rightMargin: 4
                topMargin: 4
                bottomMargin: 4
                clip: true
                spacing: 4
                visible: page.network !== null && page.network.wifiEnabled && count > 0 && !page.joiningHidden
                model: page.network ? page.network.wifiModel : null
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: false
                T.ScrollBar.vertical: UI.ScrollBar {}

                delegate: ListRow {
                    id: row

                    required property int index
                    required property var model

                    readonly property int connState: model.ConnectionState
                    readonly property bool saved: (model.Uuid || "").length > 0
                    readonly property int security: model.SecurityType
                    readonly property bool secured: security !== PlasmaNM.Enums.NoneSecurity
                        && security !== PlasmaNM.Enums.OWE && security !== PlasmaNM.Enums.UnknownSecurity
                    readonly property bool needsPassword: !saved && (security === PlasmaNM.Enums.StaticWep
                        || security === PlasmaNM.Enums.WpaPsk || security === PlasmaNM.Enums.Wpa2Psk
                        || security === PlasmaNM.Enums.SAE)

                    width: ListView.view.width - 8
                    title: model.ItemUniqueName
                    active: connState === PlasmaNM.Enums.Activated
                    expanded: page.openRow === index
                    subtitle: connState === PlasmaNM.Enums.Activated ? (secured ? "Connected, secured" : "Connected")
                        : connState === PlasmaNM.Enums.Activating ? "Connecting"
                        : connState === PlasmaNM.Enums.Deactivating ? "Disconnecting"
                        : saved ? "Saved"
                        : secured ? "Secured" : "Open"
                    icon: Component {
                        WifiGlyph {
                            bars: row.model.Signal >= 70 ? 3 : row.model.Signal >= 45 ? 2 : row.model.Signal >= 20 ? 1 : 0
                            color: UI.Style.iconInk
                            strokeWidth: 1.9
                        }
                    }

                    onClicked: {
                        page.openRow = expanded ? -1 : index;
                        if (expanded && needsPassword) {
                            password.forceActiveFocus();
                        }
                    }
                    onExpandedChanged: {
                        // Holding the row still while a password is typed
                        // stops the list re-sorting under the pointer.
                        page.network.setHoldUpdates(index, expanded && needsPassword && connState === PlasmaNM.Enums.Deactivated);
                        if (!expanded) {
                            password.text = "";
                        }
                    }
                    Component.onDestruction: {
                        if (expanded && page.network) {
                            page.network.setHoldUpdates(index, false);
                        }
                    }

                    function connect(): void {
                        if (saved) {
                            page.network.activate(model.ConnectionPath, model.DevicePath, model.SpecificPath);
                        } else {
                            page.network.addAndActivate(model.DevicePath, model.SpecificPath, needsPassword ? password.text : "");
                        }
                        page.failure = "";
                        page.openRow = -1;
                    }

                    UI.TextField {
                        id: password
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        visible: row.needsPassword && row.connState === PlasmaNM.Enums.Deactivated
                        revealable: true
                        UI.NoPaste {}
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        placeholderText: "Password"
                        Accessible.name: "Password for " + row.title
                        onAccepted: {
                            if (connectButton.enabled) {
                                row.connect();
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        UI.Button {
                            size: "small"
                            id: connectButton
                            Layout.fillWidth: true
                            visible: row.connState === PlasmaNM.Enums.Deactivated
                            kind: "primary"
                            text: "Connect"
                            // WPA needs 8 to 63 characters; WEP keys are shorter.
                            enabled: !password.visible || password.text.length >= (row.security === PlasmaNM.Enums.StaticWep ? 5 : 8)
                            onClicked: row.connect()
                        }

                        UI.Button {
                            size: "small"
                            Layout.fillWidth: true
                            visible: row.connState !== PlasmaNM.Enums.Deactivated
                            text: row.connState === PlasmaNM.Enums.Activating ? "Cancel" : "Disconnect"
                            onClicked: {
                                page.network.deactivate(row.model.ConnectionPath, row.model.DevicePath);
                                page.openRow = -1;
                            }
                        }

                        UI.Button {
                            size: "small"
                            Layout.fillWidth: true
                            visible: row.saved
                            text: "Forget"
                            onClicked: {
                                page.network.forget(row.model.ConnectionPath);
                                page.openRow = -1;
                            }
                        }
                    }
                }
            }

            // EmptyStates.dc: why in one line, what to do, and one button.
            Placeholder {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 320)
                visible: !list.visible && !page.joiningHidden
                iconPath: page.network !== null && page.network.flightMode && !page.network.wifiEnabled ? Icons.flight : Icons.wifi
                iconFill: page.network !== null && page.network.flightMode && !page.network.wifiEnabled ? "" : Icons.wifiDot
                heading: page.network === null ? "Wi-Fi is not available"
                    : !page.network.wifiEnabled && page.network.flightMode ? "Flight mode is on"
                    : !page.network.wifiEnabled ? "Wi-Fi is off"
                    : page.network.scanning ? "Looking for networks" : "No networks found"
                explanation: page.network === null ? "Network management isn't running, so Wi-Fi can't be used right now."
                    : !page.network.wifiEnabled && page.network.flightMode ? "Wireless connections are off. Turn off flight mode to use Wi-Fi."
                    : !page.network.wifiEnabled ? "Turn it on to see networks nearby."
                    : "Move closer to your router, or check it's switched on. You can also plug in a network cable."
                actionText: page.network === null ? ""
                    : !page.network.wifiEnabled && page.network.flightMode ? "Turn off flight mode"
                    : !page.network.wifiEnabled ? "Turn on Wi-Fi"
                    : "Search again"
                busy: page.network !== null && page.network.wifiEnabled && page.network.scanning
                onAction: {
                    if (!page.network.wifiEnabled && page.network.flightMode) {
                        page.network.setFlightMode(false);
                    } else if (!page.network.wifiEnabled) {
                        page.network.setWifiEnabled(true);
                    } else {
                        page.network.requestScan();
                    }
                }
            }

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: page.joiningHidden
                spacing: 10

                UI.Label {
                    Layout.fillWidth: true
                    text: "Join a hidden network"
                    size: 15
                    font.weight: UI.Style.weightSemiBold
                    Accessible.role: Accessible.Heading
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: "Type the network's name exactly as it was set up on the router."
                    color: UI.Style.inkMuted
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }

                UI.TextField {
                    id: hiddenName
                    Layout.fillWidth: true
                    placeholderText: "Network name"
                    onAccepted: hiddenPassword.forceActiveFocus()
                }

                UI.TextField {
                    id: hiddenPassword
                    Layout.fillWidth: true
                    revealable: true
                    UI.NoPaste {}
                    placeholderText: "Password, if it has one"
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    onAccepted: {
                        if (joinButton.enabled) {
                            page.joinHidden();
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 8

                    UI.Button {
                        text: "Cancel"
                        onClicked: page.joiningHidden = false
                    }

                    UI.Button {
                        id: joinButton
                        kind: "primary"
                        text: "Join"
                        busy: page.joinBusy
                        enabled: hiddenName.text.length > 0 && !page.joinBusy
                            && (hiddenPassword.text.length === 0 || hiddenPassword.text.length >= 8)
                        onClicked: page.joinHidden()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            UI.LinkButton {
                Layout.leftMargin: -4
                visible: !page.joiningHidden && page.network !== null && page.network.wifiEnabled
                text: "Join a hidden network"
                onClicked: {
                    page.joiningHidden = true;
                    hiddenName.forceActiveFocus();
                }
            }

            Item {
                Layout.fillWidth: true
            }

            UI.LinkButton {
                Layout.rightMargin: -4
                text: "Network settings"
                onClicked: page.quick.openSettings("network")
            }
        }
    }
}
