import QtQuick
import QtCore
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Advanced network settings, laid out as SettingsNetwork.dc.html: VPN and
// saved Wi-Fi networks on the left, the network in use and the proxy on the
// right. "See all" and qsTr("Hardware details") open inside this page and stack
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
    readonly property var subTitles: view === "saved" ? [qsTr("Saved Wi-Fi networks")] : view === "hardware" ? [qsTr("Hardware details")] : []

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
            return qsTr("just now");
        }
        const hours = Math.floor(minutes / 60);
        if (hours === 0) {
            return qsTr("%1 min").arg(minutes);
        }
        return minutes % 60 === 0 ? qsTr("%1 h").arg(hours) : qsTr("%1 h %2 min").arg(hours).arg(minutes % 60);
    }

    function vpnDescription(vpn) {
        page.now;
        if (vpn.state === "on") {
            return vpn.since > 0 ? qsTr("%1. Connected %2").arg(vpn.kind).arg(duration(Date.now() - vpn.since)) : qsTr("%1. Connected").arg(vpn.kind);
        }
        if (vpn.state === "connecting") {
            return qsTr("%1. Connecting").arg(vpn.kind);
        }
        if (vpn.state === "dropped") {
            return qsTr("%1. Dropped, so the internet is blocked until it connects again").arg(vpn.kind);
        }
        return vpn.server.length > 0 ? vpn.kind + ". " + vpn.server : vpn.kind;
    }

    function lastUsed(ms) {
        page.now;
        if (!ms) {
            return qsTr("Not used yet");
        }
        const then = new Date(ms);
        const today = new Date();
        const midnight = new Date(today.getFullYear(), today.getMonth(), today.getDate());
        const days = Math.ceil((midnight - new Date(then.getFullYear(), then.getMonth(), then.getDate())) / 86400000);
        if (days <= 0) {
            return qsTr("Last used today");
        }
        if (days === 1) {
            return qsTr("Last used yesterday");
        }
        if (days < 7) {
            return qsTr("Last used %1 days ago").arg(days);
        }
        if (days < 14) {
            return qsTr("Last used last week");
        }
        if (days < 31) {
            return qsTr("Last used %1 weeks ago").arg(Math.floor(days / 7));
        }
        if (days < 365) {
            return qsTr("Last used in %1").arg(Format.months[then.getMonth()]);
        }
        return qsTr("Last used over a year ago");
    }

    function wifiDescription(net) {
        const parts = [];
        if (net.active) {
            parts.push(qsTr("Connected"));
        } else if (net.connecting) {
            parts.push(qsTr("Connecting"));
        }
        if (net.metered) {
            parts.push(qsTr("Metered"));
        }
        if (net.active) {
            if (net.autoconnect) {
                parts.push(qsTr("Joins automatically"));
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
            return qsTr("%1 Mbps").arg(Math.round(kbits / 1000));
        }
        const gbps = Number((kbits / 1000000).toFixed(1));
        return qsTr("%1 Gbps").arg(gbps.toLocaleString(Qt.locale(), "f", gbps % 1 === 0 ? 0 : 1));
    }

    function frequencyText(mhz) {
        if (!mhz) {
            return "";
        }
        const band = mhz >= 5925 ? qsTr("6 GHz") : mhz >= 4900 ? qsTr("5 GHz") : qsTr("2.4 GHz");
        return qsTr("%1, %2 MHz").arg(band).arg(mhz);
    }

    readonly property string dnsDescription: {
        if (dnsShown === "automatic") {
            return qsTr("From the network, so your provider can see the sites you visit");
        }
        if (dnsShown === "custom" && !current.dnsEncrypted) {
            return qsTr("Not encrypted unless each server has its name after #");
        }
        return qsTr("Encrypted, so your provider can't see the sites you visit");
    }

    readonly property string killSwitchDescription: {
        if (!NetworkSettings.killSwitch) {
            return qsTr("Block the internet if the VPN drops");
        }
        if (NetworkSettings.killSwitchState === "blocking") {
            return qsTr("The VPN dropped, so the internet is blocked until it connects again or you turn it off");
        }
        if (NetworkSettings.killSwitchState === "mullvad") {
            return qsTr("The Mullvad app's own kill switch is guarding its connection");
        }
        return qsTr("Block the internet if the VPN drops");
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
                                status: wanted ? qsTr("On") : qsTr("Off")
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
                                    Accessible.name: qsTr("Connect to %1").arg(vpnRow.modelData.name)
                                    onToggled: NetworkSettings.setVpnActive(vpnRow.modelData.uuid, checked)
                                }
                            }
                        }

                        UI.SettingRow {
                            minHeight: 58
                            title: qsTr("Add a VPN")
                            description: qsTr("Import a WireGuard or OpenVPN file, or type the details")

                            UI.Button {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Add")
                                busy: page.busy === "import"
                                enabled: page.busy.length === 0
                                onClicked: addVpnDialog.open()
                            }
                        }

                        UI.SettingRow {
                            minHeight: 58
                            title: qsTr("Kill switch")
                            description: page.killSwitchDescription
                            descriptionColor: NetworkSettings.killSwitchState === "blocking" ? UI.Style.warning : UI.Style.inkMuted
                            status: NetworkSettings.killSwitch ? qsTr("On") : qsTr("Off")

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: NetworkSettings.killSwitch
                                enabled: page.busy.length === 0
                                Accessible.name: qsTr("VPN kill switch")
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
                    text: qsTr("Saved Wi-Fi networks")
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
                            title: qsTr("No saved networks")
                            description: qsTr("Networks you join are remembered here")
                        }

                        UI.SettingRow {
                            visible: NetworkSettings.savedWifi.length > 3
                            minHeight: 44
                            title: qsTr("See all %1 networks").arg(NetworkSettings.savedWifi.length)
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
                    text: page.connected ? page.current.name : qsTr("Network in use")
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        UI.SettingRow {
                            visible: !page.connected
                            minHeight: 52
                            title: qsTr("Not connected")
                            description: qsTr("Connect to a network to change its address, DNS and other settings here")
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: qsTr("IP address")
                            description: page.current.manual ? qsTr("%1, set by you").arg(page.current.address || qsTr("No address yet")) : qsTr("%1, set automatically").arg(page.current.address || qsTr("No address yet"))

                            UI.Button {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Edit")
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
                                Accessible.name: qsTr("DNS server")
                                enabled: page.busy.length === 0 || page.busy === "dns"
                                model: [
                                    { value: "automatic", text: qsTr("Automatic") },
                                    { value: "quad9", text: qsTr("Quad9 encrypted") },
                                    { value: "cloudflare", text: qsTr("Cloudflare encrypted") },
                                    { value: "mullvad", text: qsTr("Mullvad encrypted") },
                                    { value: "custom", text: qsTr("Custom") }
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
                            title: qsTr("DNS servers")
                            description: customDnsField.text.length > 0 ? NetworkSettings.checkCustomDns(customDnsField.text)
                                : qsTr("Separate them with commas. Add #name after an address to encrypt it, as in 9.9.9.9#dns.quad9.net")
                            descriptionColor: customDnsField.text.length > 0 ? UI.Style.danger : UI.Style.inkMuted

                            UI.TextField {
                                id: customDnsField
                                anchors.verticalCenter: parent.verticalCenter
                                width: 200
                                placeholderText: qsTr("Addresses")
                                Accessible.name: qsTr("DNS servers")
                                Component.onCompleted: text = page.current.dnsCustom || ""
                                onAccepted: dnsSave.clicked()
                            }

                            UI.Button {
                                id: dnsSave
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Save")
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
                            title: qsTr("Private hardware address")
                            description: page.current.wifi ? qsTr("A new random address every time you connect") : qsTr("A random address of its own for this network")
                            status: page.current.privateAddress ? qsTr("On") : qsTr("Off")

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: page.current.privateAddress === true
                                enabled: page.busy.length === 0
                                Accessible.name: qsTr("Private hardware address")
                                onToggled: NetworkSettings.setPrivateAddress(checked)
                            }
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: qsTr("Metered connection")
                            description: qsTr("Holds back big downloads and updates")
                            status: page.current.metered ? qsTr("On") : qsTr("Off")

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: page.current.metered === true
                                enabled: page.busy.length === 0
                                Accessible.name: qsTr("Metered connection")
                                onToggled: NetworkSettings.setMetered(checked)
                            }
                        }

                        UI.SettingRow {
                            visible: page.connected
                            minHeight: 52
                            title: qsTr("Hardware details")
                            description: qsTr("Speed, frequency, IPv6 and MAC address")
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
                            title: qsTr("Use a proxy server")
                            description: qsTr("Usually only needed at work or school")

                            UI.ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: "Proxy"
                                model: [
                                    { value: "off", text: qsTr("Off") },
                                    { value: "automatic", text: qsTr("Find automatically") },
                                    { value: "script", text: qsTr("Setup script") },
                                    { value: "manual", text: qsTr("Manual") }
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
                            title: qsTr("Proxy server")

                            UI.TextField {
                                id: proxyHost
                                anchors.verticalCenter: parent.verticalCenter
                                width: 170
                                placeholderText: qsTr("Address")
                                Accessible.name: qsTr("Proxy address")
                                Component.onCompleted: text = NetworkSettings.proxy.host || ""
                                onTextEdited: page.proxyEditing = "manual"
                                onAccepted: proxySave.clicked()
                            }

                            UI.TextField {
                                id: proxyPort
                                anchors.verticalCenter: parent.verticalCenter
                                width: 76
                                placeholderText: qsTr("Port")
                                Accessible.name: qsTr("Proxy port")
                                validator: IntValidator { bottom: 1; top: 65535 }
                                Component.onCompleted: text = NetworkSettings.proxy.port > 0 ? "" + NetworkSettings.proxy.port : ""
                                onTextEdited: page.proxyEditing = "manual"
                                onAccepted: proxySave.clicked()
                            }

                            UI.Button {
                                id: proxySave
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Save")
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
                            title: qsTr("Script address")

                            UI.TextField {
                                id: proxyScript
                                anchors.verticalCenter: parent.verticalCenter
                                width: 220
                                placeholderText: qsTr("For example http://wpad/proxy.pac")
                                Accessible.name: qsTr("Setup script address")
                                Component.onCompleted: text = NetworkSettings.proxy.script || ""
                                onTextEdited: page.proxyEditing = "script"
                                onAccepted: scriptSave.clicked()
                            }

                            UI.Button {
                                id: scriptSave
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Save")
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
                title: qsTr("No saved networks")
                description: qsTr("Networks you join are remembered here")
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
                        return [{ title: qsTr("Not connected"), value: "" }];
                    }
                    rows.push({ title: qsTr("Connection"), value: c.wifi ? "Wi-Fi" : "Ethernet" });
                    if (c.speed) {
                        rows.push({ title: qsTr("Speed"), value: page.speedText(c.speed) });
                    }
                    if (c.frequency) {
                        rows.push({ title: qsTr("Frequency"), value: page.frequencyText(c.frequency) });
                    }
                    if (c.address) {
                        rows.push({ title: qsTr("IPv4 address"), value: c.address + "/" + c.prefix });
                    }
                    if (c.gateway) {
                        rows.push({ title: qsTr("Default gateway"), value: c.gateway });
                    }
                    rows.push({ title: qsTr("IPv6 address"), value: c.ipv6 && c.ipv6.length > 0 ? c.ipv6.join("\n") : qsTr("None") });
                    if (c.dnsServers && c.dnsServers.length > 0) {
                        rows.push({ title: qsTr("DNS servers"), value: c.dnsServers.join("\n") });
                    }
                    if (c.mac) {
                        rows.push({ title: qsTr("MAC address"), value: c.mac });
                    }
                    if (c.permanentMac && c.permanentMac !== c.mac) {
                        rows.push({ title: qsTr("Built-in MAC address"), value: c.permanentMac });
                    }
                    if (c.interface) {
                        rows.push({ title: qsTr("Device"), value: c.interface });
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
            text: qsTr("Forget")
            enabled: NetworkSettings.busy.length === 0
            Accessible.name: qsTr("Forget %1").arg(savedRow.network.name)
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

        title: qsTr("Add a VPN")
        text: typing ? qsTr("Type the WireGuard details your VPN provider gave you.")
            : qsTr("Import the file your VPN provider gave you: a WireGuard .conf or an OpenVPN .ovpn file. If you only have the details, type them instead.")
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
            text: qsTr("Name")
        }

        UI.TextField {
            id: vpnName
            Accessible.name: qsTr("Name")
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            placeholderText: qsTr("For example Home office")
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: qsTr("Server, with its port")
        }

        UI.TextField {
            id: vpnEndpoint
            Accessible.name: qsTr("Server, with its port")
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            placeholderText: qsTr("For example vpn.example.com:51820")
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: qsTr("Server's public key")
        }

        UI.TextField {
            id: vpnPublicKey
            Accessible.name: qsTr("Server's public key")
            Layout.fillWidth: true
            visible: addVpnDialog.typing
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: qsTr("Your private key")
        }

        UI.TextField {
            id: vpnPrivateKey
            Accessible.name: qsTr("Your private key")
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            revealable: true
            UI.NoPaste {}
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: qsTr("Your address on the VPN")
        }

        UI.TextField {
            id: vpnAddress
            Accessible.name: qsTr("Your address on the VPN")
            Layout.fillWidth: true
            visible: addVpnDialog.typing
            placeholderText: qsTr("For example 10.64.0.2/32")
        }

        FieldLabel {
            visible: addVpnDialog.typing
            text: qsTr("DNS server, if your provider gave one")
        }

        UI.TextField {
            id: vpnDns
            Accessible.name: qsTr("DNS server, if your provider gave one")
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
                text: qsTr("Type the details")
                onClicked: {
                    addVpnDialog.typing = true;
                    vpnName.forceActiveFocus();
                }
            },
            UI.Button {
                visible: !addVpnDialog.typing
                text: qsTr("Import a file")
                kind: "primary"
                onClicked: {
                    addVpnDialog.close();
                    vpnFileDialog.open();
                }
            },
            UI.Button {
                id: vpnAdd
                visible: addVpnDialog.typing
                text: qsTr("Add")
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
                text: qsTr("Cancel")
                onClicked: addVpnDialog.close()
            }
        ]
    }

    FileDialog {
        id: vpnFileDialog
        title: qsTr("Import a VPN")
        nameFilters: [qsTr("WireGuard and OpenVPN files (*.conf *.ovpn)"), qsTr("All files (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: NetworkSettings.importVpn(selectedFile)
    }

    UI.Dialog {
        id: ipDialog

        readonly property bool manual: ipMode.currentValue === "manual"
        readonly property string problem: manual ? NetworkSettings.checkAddress(ipAddress.text, ipMask.text, ipGateway.text) : ""

        title: qsTr("Edit IP address")
        text: qsTr("Most networks give this computer an address automatically. Set one yourself only if the network needs it.")
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
                text: qsTr("Set the address")
            }

            UI.ComboBox {
                id: ipMode
                Accessible.name: qsTr("Set the address")
                model: [{ value: "auto", text: qsTr("Automatically") }, { value: "manual", text: qsTr("Myself") }]
            }
        }

        FieldLabel {
            visible: ipDialog.manual
            text: qsTr("IP address")
        }

        UI.TextField {
            id: ipAddress
            Accessible.name: qsTr("IP address")
            Layout.fillWidth: true
            visible: ipDialog.manual
            placeholderText: qsTr("For example 192.168.1.42")
        }

        FieldLabel {
            visible: ipDialog.manual
            text: qsTr("Subnet mask")
        }

        UI.TextField {
            id: ipMask
            Accessible.name: qsTr("Subnet mask")
            Layout.fillWidth: true
            visible: ipDialog.manual
            placeholderText: qsTr("For example 255.255.255.0")
        }

        FieldLabel {
            visible: ipDialog.manual
            text: qsTr("Gateway")
        }

        UI.TextField {
            id: ipGateway
            Accessible.name: qsTr("Gateway")
            Layout.fillWidth: true
            visible: ipDialog.manual
            placeholderText: qsTr("For example 192.168.1.1")
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
                text: qsTr("Save")
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
                text: qsTr("Cancel")
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
