import QtQuick
import QtCore
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Advanced network settings, laid out as SettingsNetwork.dc.html: VPN and
// saved Wi-Fi networks on the left, the network in use and the proxy on the
// right. "See all" and "Hardware details" open inside this page and stack
// in the breadcrumb, as Printers does.
//
// Everything shown is bound to NetworkSettings, which outlives the page, so
// coming back to it rebuilds the same thing; nothing here is set up once
// and kept.
PageScroll {
    id: page

    // "", "saved" or "hardware"
    property string view: ""
    readonly property int depth: view.length > 0 ? 2 : 1
    readonly property var subTitles: view === "saved" ? ["Saved Wi-Fi networks"] : view === "hardware" ? ["Hardware details"] : []

    function back() {
        if (view.length > 0) {
            view = "";
            return true;
        }
        return false;
    }

    readonly property var current: NetworkSettings.current
    readonly property bool connected: current.uuid !== undefined
    readonly property string busy: NetworkSettings.busy

    // A choice that needs more typed before it is saved.
    property bool customDnsOpen: false
    property string proxyEditing: ""
    readonly property string dnsShown: customDnsOpen ? "custom" : (current.dns || "automatic")
    readonly property string proxyShown: proxyEditing.length > 0 ? proxyEditing : NetworkSettings.proxy.mode

    // Keeps "Connected 2 h 14 min" and "Last used" true while the page is
    // open, and catches changes NetworkManager makes without a signal.
    property date now: new Date()
    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: {
            page.now = new Date();
            NetworkSettings.refresh();
        }
    }

    Component.onCompleted: NetworkSettings.refresh()

    function duration(ms) {
        const minutes = Math.floor(ms / 60000);
        if (minutes < 1) {
            return "just now";
        }
        const hours = Math.floor(minutes / 60);
        if (hours === 0) {
            return minutes + " min";
        }
        return minutes % 60 === 0 ? hours + " h" : hours + " h " + (minutes % 60) + " min";
    }

    function vpnDescription(vpn) {
        page.now;
        if (vpn.state === "on") {
            return vpn.kind + ". Connected" + (vpn.since > 0 ? " " + duration(Date.now() - vpn.since) : "");
        }
        if (vpn.state === "connecting") {
            return vpn.kind + ". Connecting";
        }
        if (vpn.state === "dropped") {
            return vpn.kind + ". Dropped, so the internet is blocked until it connects again";
        }
        return vpn.server.length > 0 ? vpn.kind + ". " + vpn.server : vpn.kind;
    }

    function lastUsed(ms) {
        page.now;
        if (!ms) {
            return "Not used yet";
        }
        const then = new Date(ms);
        const today = new Date();
        const midnight = new Date(today.getFullYear(), today.getMonth(), today.getDate());
        const days = Math.ceil((midnight - new Date(then.getFullYear(), then.getMonth(), then.getDate())) / 86400000);
        if (days <= 0) {
            return "Last used today";
        }
        if (days === 1) {
            return "Last used yesterday";
        }
        if (days < 7) {
            return "Last used " + days + " days ago";
        }
        if (days < 14) {
            return "Last used last week";
        }
        if (days < 31) {
            return "Last used " + Math.floor(days / 7) + " weeks ago";
        }
        if (days < 365) {
            return "Last used in " + Format.months[then.getMonth()];
        }
        return "Last used over a year ago";
    }

    function wifiDescription(net) {
        const parts = [];
        if (net.active) {
            parts.push("Connected");
        } else if (net.connecting) {
            parts.push("Connecting");
        }
        if (net.metered) {
            parts.push("Metered");
        }
        if (net.active) {
            if (net.autoconnect) {
                parts.push("Joins automatically");
            }
        } else if (!net.connecting) {
            parts.push(lastUsed(net.lastUsed));
        }
        return parts.join(". ");
    }

    function speedText(kbits) {
        if (!kbits) {
            return "";
        }
        if (kbits < 1000000) {
            return Math.round(kbits / 1000) + " Mbps";
        }
        const gbps = Number((kbits / 1000000).toFixed(1));
        return gbps.toLocaleString(Qt.locale(), "f", gbps % 1 === 0 ? 0 : 1) + " Gbps";
    }

    function frequencyText(mhz) {
        if (!mhz) {
            return "";
        }
        const band = mhz >= 5925 ? "6 GHz" : mhz >= 4900 ? "5 GHz" : "2.4 GHz";
        return band + ", " + mhz + " MHz";
    }

    readonly property string dnsDescription: {
        if (dnsShown === "automatic") {
            return "From the network, so your provider can see the sites you visit";
        }
        if (dnsShown === "custom" && !current.dnsEncrypted) {
            return "Not encrypted unless each server has its name after #";
        }
        return "Encrypted, so your provider can't see the sites you visit";
    }

    readonly property string killSwitchDescription: {
        if (!NetworkSettings.killSwitch) {
            return "Block the internet if the VPN drops";
        }
        if (NetworkSettings.killSwitchState === "blocking") {
            return "The VPN dropped, so the internet is blocked until it connects again or you turn it off";
        }
        if (NetworkSettings.killSwitchState === "mullvad") {
            return "The Mullvad app's own kill switch is guarding its connection";
        }
        return "Block the internet if the VPN drops";
    }

    Connections {
        target: NetworkSettings
        function onDone(title, message) {
            notice.show(title, message, false);
        }
        function onFailed(title, reason) {
            notice.show(title, reason, true);
            page.customDnsOpen = false;
        }
    }

    Notice {
        id: notice
    }

    GridLayout {
        Layout.fillWidth: true
        visible: page.view === ""
        columns: width >= 820 ? 2 : 1
        columnSpacing: 20
        rowSpacing: Metrics.sectionGap

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                SectionTitle {
                    text: "VPN"
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        Repeater {
                            model: NetworkSettings.vpns

                            UI.SettingRow {
                                id: vpnRow
                                required property var modelData
                                readonly property bool wanted: modelData.state !== "off"
                                readonly property bool up: modelData.state === "on"

                                minHeight: 58
                                title: modelData.name
                                description: page.vpnDescription(modelData)
                                descriptionColor: modelData.state === "dropped" ? UI.Style.warning : UI.Style.inkMuted
                                status: wanted ? "On" : "Off"
                                leading: Rectangle {
                                    width: 34
                                    height: 34
                                    radius: 9
                                    color: vpnRow.up ? UI.Style.successSoft : UI.Style.surfaceTile

                                    UI.Icon {
                                        anchors.centerIn: parent
                                        name: "lock"
                                        size: 16
                                        strokeWidth: 1.8
                                        color: vpnRow.up ? UI.Style.success : UI.Style.iconInk
                                    }
                                }

                                UI.Toggle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: vpnRow.wanted
                                    Accessible.name: "Connect to " + vpnRow.modelData.name
                                    onToggled: NetworkSettings.setVpnActive(vpnRow.modelData.uuid, checked)
                                }
                            }
                        }

                        UI.SettingRow {
                            minHeight: 58
                            title: "Add a VPN"
                            description: "Import a WireGuard or OpenVPN file, or type the details"

                            UI.Button {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Add"
                                busy: page.busy === "import"
                                enabled: page.busy.length === 0
                                onClicked: addVpnDialog.open()
                            }
                        }

                        UI.SettingRow {
                            minHeight: 58
                            title: "Kill switch"
                            description: page.killSwitchDescription
                            descriptionColor: NetworkSettings.killSwitchState === "blocking" ? UI.Style.warning : UI.Style.inkMuted
                            status: NetworkSettings.killSwitch ? "On" : "Off"

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: NetworkSettings.killSwitch
                                enabled: page.busy.length === 0
                                Accessible.name: "VPN kill switch"
                                onToggled: NetworkSettings.setKillSwitch(checked)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                SectionTitle {
                    text: "Saved Wi-Fi networks"
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        Repeater {
                            model: NetworkSettings.savedWifi.slice(0, 3)

                            SavedNetworkRow {
                                required property var modelData
                                network: modelData
                                description: page.wifiDescription(modelData)
                            }
                        }

                        UI.SettingRow {
                            visible: NetworkSettings.savedWifi.length === 0
                            minHeight: 46
                            title: "No saved networks"
                            description: "Networks you join are remembered here"
                        }

                        UI.SettingRow {
                            visible: NetworkSettings.savedWifi.length > 3
                            minHeight: 44
                            title: "See all " + NetworkSettings.savedWifi.length + " networks"
                            chevron: true
                            onClicked: page.view = "saved"
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                SectionTitle {
                    text: page.connected ? page.current.name : "Network in use"
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        UI.SettingRow {
                            visible: !page.connected
                            minHeight: 52
                            title: "Not connected"
                            description: "Connect to a network to change its address, DNS and other settings here"
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: "IP address"
                            description: (page.current.address || "No address yet") + (page.current.manual ? ", set by you" : ", set automatically")

                            UI.Button {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Edit"
                                busy: page.busy === "address"
                                enabled: page.busy.length === 0
                                onClicked: ipDialog.open()
                            }
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: "DNS"
                            description: page.dnsDescription

                            UI.ComboBox {
                                id: dnsBox
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: "DNS server"
                                enabled: page.busy.length === 0 || page.busy === "dns"
                                model: [
                                    { value: "automatic", text: "Automatic" },
                                    { value: "quad9", text: "Quad9 encrypted" },
                                    { value: "cloudflare", text: "Cloudflare encrypted" },
                                    { value: "mullvad", text: "Mullvad encrypted" },
                                    { value: "custom", text: "Custom" }
                                ]
                                currentIndex: model.findIndex(o => o.value === page.dnsShown)
                                onActivated: index => {
                                    const choice = valueAt(index);
                                    if (choice === "custom") {
                                        customDnsField.text = page.current.dnsCustom || "";
                                        page.customDnsOpen = true;
                                        customDnsField.forceActiveFocus();
                                    } else {
                                        page.customDnsOpen = false;
                                        if (choice !== page.current.dns) {
                                            NetworkSettings.setDns(choice);
                                        }
                                    }
                                }
                            }
                        }

                        UI.SettingRow {
                            visible: page.connected && page.dnsShown === "custom"
                            minHeight: 52
                            title: "DNS servers"
                            description: customDnsField.text.length > 0 ? NetworkSettings.checkCustomDns(customDnsField.text)
                                : "Separate them with commas. Add #name after an address to encrypt it, as in 9.9.9.9#dns.quad9.net"
                            descriptionColor: customDnsField.text.length > 0 ? UI.Style.danger : UI.Style.inkMuted

                            UI.TextField {
                                id: customDnsField
                                anchors.verticalCenter: parent.verticalCenter
                                width: 200
                                placeholderText: "Addresses"
                                Accessible.name: "DNS servers"
                                Component.onCompleted: text = page.current.dnsCustom || ""
                                onAccepted: dnsSave.clicked()
                            }

                            UI.Button {
                                id: dnsSave
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Save"
                                kind: "primary"
                                busy: page.busy === "dns"
                                enabled: page.busy.length === 0 && NetworkSettings.checkCustomDns(customDnsField.text).length === 0
                                    && (page.customDnsOpen || customDnsField.text !== page.current.dnsCustom)
                                onClicked: {
                                    if (!enabled) {
                                        return;
                                    }
                                    page.customDnsOpen = false;
                                    NetworkSettings.setDns("custom", customDnsField.text);
                                }
                            }
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: "Private hardware address"
                            description: page.current.wifi ? "A new random address every time you connect" : "A random address of its own for this network"
                            status: page.current.privateAddress ? "On" : "Off"

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: page.current.privateAddress === true
                                enabled: page.busy.length === 0
                                Accessible.name: "Private hardware address"
                                onToggled: NetworkSettings.setPrivateAddress(checked)
                            }
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: "Metered connection"
                            description: "Holds back big downloads and updates"
                            status: page.current.metered ? "On" : "Off"

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: page.current.metered === true
                                enabled: page.busy.length === 0
                                Accessible.name: "Metered connection"
                                onToggled: NetworkSettings.setMetered(checked)
                            }
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: "Hardware details"
                            description: "Speed, frequency, IPv6 and MAC address"
                            chevron: true
                            onClicked: page.view = "hardware"
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                SectionTitle {
                    text: "Proxy"
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        UI.SettingRow {
                            minHeight: 52
                            title: "Use a proxy server"
                            description: "Usually only needed at work or school"

                            UI.ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: "Proxy"
                                model: [
                                    { value: "off", text: "Off" },
                                    { value: "automatic", text: "Find automatically" },
                                    { value: "script", text: "Setup script" },
                                    { value: "manual", text: "Manual" }
                                ]
                                currentIndex: model.findIndex(o => o.value === page.proxyShown)
                                onActivated: index => {
                                    const mode = valueAt(index);
                                    if (mode === "manual" || mode === "script") {
                                        proxyHost.text = NetworkSettings.proxy.host || "";
                                        proxyPort.text = NetworkSettings.proxy.port > 0 ? "" + NetworkSettings.proxy.port : "";
                                        proxyScript.text = NetworkSettings.proxy.script || "";
                                        page.proxyEditing = mode;
                                    } else {
                                        page.proxyEditing = "";
                                        NetworkSettings.setProxy(mode);
                                    }
                                }
                            }
                        }

                        UI.SettingRow {
                            visible: page.proxyShown === "manual"
                            minHeight: 52
                            title: "Proxy server"

                            UI.TextField {
                                id: proxyHost
                                anchors.verticalCenter: parent.verticalCenter
                                width: 170
                                placeholderText: "Address"
                                Accessible.name: "Proxy address"
                                Component.onCompleted: text = NetworkSettings.proxy.host || ""
                                onTextEdited: page.proxyEditing = "manual"
                                onAccepted: proxySave.clicked()
                            }

                            UI.TextField {
                                id: proxyPort
                                anchors.verticalCenter: parent.verticalCenter
                                width: 76
                                placeholderText: "Port"
                                Accessible.name: "Proxy port"
                                validator: IntValidator { bottom: 1; top: 65535 }
                                Component.onCompleted: text = NetworkSettings.proxy.port > 0 ? "" + NetworkSettings.proxy.port : ""
                                onTextEdited: page.proxyEditing = "manual"
                                onAccepted: proxySave.clicked()
                            }

                            UI.Button {
                                id: proxySave
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Save"
                                kind: "primary"
                                visible: page.proxyEditing === "manual"
                                enabled: proxyHost.text.trim().length > 0 && parseInt(proxyPort.text) > 0
                                onClicked: {
                                    if (!enabled) {
                                        return;
                                    }
                                    NetworkSettings.setProxy("manual", proxyHost.text.trim(), parseInt(proxyPort.text));
                                    page.proxyEditing = "";
                                }
                            }
                        }

                        UI.SettingRow {
                            visible: page.proxyShown === "script"
                            minHeight: 52
                            title: "Script address"

                            UI.TextField {
                                id: proxyScript
                                anchors.verticalCenter: parent.verticalCenter
                                width: 220
                                placeholderText: "For example http://wpad/proxy.pac"
                                Accessible.name: "Setup script address"
                                Component.onCompleted: text = NetworkSettings.proxy.script || ""
                                onTextEdited: page.proxyEditing = "script"
                                onAccepted: scriptSave.clicked()
                            }

                            UI.Button {
                                id: scriptSave
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Save"
                                kind: "primary"
                                visible: page.proxyEditing === "script"
                                enabled: proxyScript.text.trim().length > 0
                                onClicked: {
                                    if (!enabled) {
                                        return;
                                    }
                                    NetworkSettings.setProxy("script", "", 0, proxyScript.text.trim());
                                    page.proxyEditing = "";
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.view === "saved"

        Column {
            width: parent.width

            Repeater {
                model: page.view === "saved" ? NetworkSettings.savedWifi : []

                SavedNetworkRow {
                    required property var modelData
                    network: modelData
                    description: page.wifiDescription(modelData)
                }
            }

            UI.SettingRow {
                visible: NetworkSettings.savedWifi.length === 0
                minHeight: 46
                title: "No saved networks"
                description: "Networks you join are remembered here"
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.view === "hardware"

        Column {
            width: parent.width

            Repeater {
                model: {
                    const c = page.current;
                    const rows = [];
                    if (!page.connected) {
                        return [{ title: "Not connected", value: "" }];
                    }
                    rows.push({ title: "Connection", value: c.wifi ? "Wi-Fi" : "Ethernet" });
                    if (c.speed) {
                        rows.push({ title: "Speed", value: page.speedText(c.speed) });
                    }
                    if (c.frequency) {
                        rows.push({ title: "Frequency", value: page.frequencyText(c.frequency) });
                    }
                    if (c.address) {
                        rows.push({ title: "IPv4 address", value: c.address + "/" + c.prefix });
                    }
                    if (c.gateway) {
                        rows.push({ title: "Default gateway", value: c.gateway });
                    }
                    rows.push({ title: "IPv6 address", value: c.ipv6 && c.ipv6.length > 0 ? c.ipv6.join("\n") : "None" });
                    if (c.dnsServers && c.dnsServers.length > 0) {
                        rows.push({ title: "DNS servers", value: c.dnsServers.join("\n") });
                    }
                    if (c.mac) {
                        rows.push({ title: "MAC address", value: c.mac });
                    }
                    if (c.permanentMac && c.permanentMac !== c.mac) {
                        rows.push({ title: "Built-in MAC address", value: c.permanentMac });
                    }
                    if (c.interface) {
                        rows.push({ title: "Device", value: c.interface });
                    }
                    return rows;
                }

                UI.SettingRow {
                    id: detailRow
                    required property var modelData
                    minHeight: 46
                    title: modelData.title
                    Accessible.description: modelData.value

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: detailRow.modelData.value
                        // Several addresses go one to a line.
                        wrapMode: Text.Wrap
                        size: 13
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }
    }

    // An inline component cannot see this page's ids, so the row is handed
    // its wording.
    component SavedNetworkRow: UI.SettingRow {
        id: savedRow

        property var network: ({})

        minHeight: 46
        title: network.name || ""

        UI.LinkButton {
            anchors.verticalCenter: parent.verticalCenter
            text: "Forget"
            enabled: NetworkSettings.busy.length === 0
            Accessible.name: "Forget " + savedRow.network.name
            onClicked: NetworkSettings.forget(savedRow.network.uuid)
        }
    }

    UI.Dialog {
        id: addVpnDialog

        property bool typing: false
        readonly property var details: ({
            name: vpnName.text,
            endpoint: vpnEndpoint.text,
            publicKey: vpnPublicKey.text,
            privateKey: vpnPrivateKey.text,
            address: vpnAddress.text,
            dns: vpnDns.text
        })
        readonly property string problem: typing ? NetworkSettings.checkWireGuard(details) : ""
        readonly property bool started: vpnName.text.length > 0 || vpnEndpoint.text.length > 0 || vpnPublicKey.text.length > 0
            || vpnPrivateKey.text.length > 0 || vpnAddress.text.length > 0

        title: "Add a VPN"
        text: typing ? "Type the WireGuard details your VPN provider gave you."
            : "Import the file your VPN provider gave you: a WireGuard .conf or an OpenVPN .ovpn file. If you only have the details, type them instead."
        onAboutToShow: {
            typing = false;
            vpnName.text = "";
            vpnEndpoint.text = "";
            vpnPublicKey.text = "";
            vpnPrivateKey.text = "";
            vpnAddress.text = "";
            vpnDns.text = "";
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: "Name"
        }

        UI.TextField {
            id: vpnName
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            placeholderText: "For example Home office"
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: "Server, with its port"
        }

        UI.TextField {
            id: vpnEndpoint
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            placeholderText: "For example vpn.example.com:51820"
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: "Server's public key"
        }

        UI.TextField {
            id: vpnPublicKey
            Layout.fillWidth: true
            visible: addVpnDialog.typing
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: "Your private key"
        }

        UI.TextField {
            id: vpnPrivateKey
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            revealable: true
            UI.NoPaste {}
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: "Your address on the VPN"
        }

        UI.TextField {
            id: vpnAddress
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            placeholderText: "For example 10.64.0.2/32"
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: "DNS server, if your provider gave one"
        }

        UI.TextField {
            id: vpnDns
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            onAccepted: vpnAdd.clicked()
        }

        UI.Label {
            Layout.fillWidth: true
            visible: addVpnDialog.started && text.length > 0
            text: addVpnDialog.problem
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                visible: !addVpnDialog.typing
                text: "Type the details"
                onClicked: {
                    addVpnDialog.typing = true;
                    vpnName.forceActiveFocus();
                }
            },
            UI.Button {
                visible: !addVpnDialog.typing
                text: "Import a file"
                kind: "primary"
                onClicked: {
                    addVpnDialog.close();
                    vpnFileDialog.open();
                }
            },
            UI.Button {
                id: vpnAdd
                visible: addVpnDialog.typing
                text: "Add"
                kind: "primary"
                enabled: addVpnDialog.problem.length === 0 && page.busy.length === 0
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    NetworkSettings.addWireGuard(addVpnDialog.details);
                    addVpnDialog.close();
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: addVpnDialog.close()
            }
        ]
    }

    FileDialog {
        id: vpnFileDialog
        title: "Import a VPN"
        nameFilters: ["WireGuard and OpenVPN files (*.conf *.ovpn)", "All files (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: NetworkSettings.importVpn(selectedFile)
    }

    UI.Dialog {
        id: ipDialog

        readonly property bool manual: ipMode.currentValue === "manual"
        readonly property string problem: manual ? NetworkSettings.checkAddress(ipAddress.text, ipMask.text, ipGateway.text) : ""

        title: "Edit IP address"
        text: "Most networks give this computer an address automatically. Set one yourself only if the network needs it."
        onAboutToShow: {
            ipMode.currentIndex = page.current.manual ? 1 : 0;
            ipAddress.text = page.current.manualAddress || page.current.address || "";
            const prefix = page.current.manualPrefix || page.current.prefix || 24;
            ipMask.text = maskFromPrefix(prefix);
            ipGateway.text = page.current.manualGateway || page.current.gateway || "";
        }

        function maskFromPrefix(prefix) {
            const parts = [];
            for (let i = 0; i < 4; ++i) {
                const bits = Math.max(0, Math.min(8, prefix - i * 8));
                parts.push(256 - Math.pow(2, 8 - bits));
            }
            return parts.join(".");
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            UI.Label {
                Layout.fillWidth: true
                text: "Set the address"
            }

            UI.ComboBox {
                id: ipMode
                Accessible.name: "Set the address"
                model: [{ value: "auto", text: "Automatically" }, { value: "manual", text: "Myself" }]
            }
        }

        FieldLabel {
            visible: ipDialog.manual
            text: "IP address"
        }

        UI.TextField {
            id: ipAddress
            Layout.fillWidth: true
            visible: ipDialog.manual
            placeholderText: "For example 192.168.1.42"
        }

        FieldLabel {
            visible: ipDialog.manual
            text: "Subnet mask"
        }

        UI.TextField {
            id: ipMask
            Layout.fillWidth: true
            visible: ipDialog.manual
            placeholderText: "For example 255.255.255.0"
        }

        FieldLabel {
            visible: ipDialog.manual
            text: "Gateway"
        }

        UI.TextField {
            id: ipGateway
            Layout.fillWidth: true
            visible: ipDialog.manual
            placeholderText: "For example 192.168.1.1"
            onAccepted: ipSave.clicked()
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: ipDialog.problem
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: ipSave
                text: "Save"
                kind: "primary"
                enabled: ipDialog.problem.length === 0 && page.busy.length === 0
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    NetworkSettings.setAddress(!ipDialog.manual, ipAddress.text, ipMask.text, ipGateway.text);
                    ipDialog.close();
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: ipDialog.close()
            }
        ]
    }

    component FieldLabel: UI.Label {
        Layout.fillWidth: true
        size: 12
        color: UI.Style.inkMuted
    }
}
