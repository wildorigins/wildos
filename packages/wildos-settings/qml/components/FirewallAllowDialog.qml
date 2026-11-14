import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Allow an app or port (FirewallAllow.dc). An app is one of the programs
// waiting for connections right now that the firewall turns away (asked of
// wildos-network, which can name them all); a port is any number. Who can
// connect is the devices on this network, one device, or anyone; for how
// long is until it is turned off, until the next restart or for an hour.
// Allowed rules never apply on a public network (settings-firewall).
RecoveryDialog {
    id: dialog

    property string networkName
    property string what: "app"
    property int appIndex: 0
    property string portText
    property string scope: "network"
    property string address
    property string duration: "forever"
    property string error
    // Opened from "What the firewall blocked": {port, proto, source, kind}.
    property var preset: null

    readonly property var apps: Firewall.listening
    readonly property var app: what === "app" && apps.length > 0 ? apps[Math.min(appIndex, apps.length - 1)] : null
    readonly property int port: what === "app" ? (app ? app.port : 0) : (/^\d{1,5}$/.test(portText.trim()) ? parseInt(portText.trim()) : 0)
    readonly property string addressProblem: scope === "device" ? Firewall.checkAddress(address) : ""
    readonly property bool ready: port > 0 && port < 65536 && addressProblem.length === 0

    function openFor(entry) {
        preset = entry || null;
        what = "app";
        appIndex = 0;
        portText = "";
        scope = "network";
        address = "";
        duration = "forever";
        error = "";
        if (preset) {
            // The device that knocked, when it is on a network nearby; the
            // internet as a whole is never offered from here.
            if (preset.kind === "local" || preset.kind === "other") {
                scope = "device";
                address = preset.source;
            }
            what = "port";
            portText = String(preset.port);
        }
        Firewall.refreshListening();
        open();
    }

    function pickPreset() {
        if (!preset) {
            return;
        }
        const i = apps.findIndex(a => a.port === preset.port && a.proto === preset.proto);
        if (i >= 0) {
            what = "app";
            appIndex = i;
        }
    }

    function submit() {
        if (!ready || Firewall.busy) {
            return;
        }
        error = "";
        const known = Firewall.serviceName(port, app ? app.proto : "tcp");
        const name = app ? app.name : known.length > 0 ? known : "Port " + port;
        Firewall.allow(name, port, app ? app.proto : "both", scope, scope === "device" ? address : "", duration);
    }

    title: "Allow an app or port"
    iconName: "shield-round-check"
    dialogWidth: 560

    Connections {
        target: Firewall
        function onListeningChanged() {
            dialog.pickPreset();
        }
        function onDone(what) {
            if (what === "allow" && dialog.opened) {
                dialog.close();
            }
        }
        function onFailed(message) {
            if (dialog.opened) {
                dialog.error = message;
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        text: dialog.what === "app"
            ? "Apps listed here are running now and waiting for other devices to connect. Until you allow one, the firewall turns those connections away."
            : "Other devices can connect to whatever on this PC waits on the port you give. Allow only a port an app you trust uses."
        size: 13
        lineHeightMode: Text.FixedHeight
        lineHeight: 19.5
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        UI.Label {
            Layout.fillWidth: true
            text: "WHAT TO ALLOW"
            size: 12.5
            font.weight: UI.Style.weightBold
            font.letterSpacing: 0.25
            color: UI.Style.inkSubtle
        }

        Segmented {
            options: [{ value: "app", text: "An app" }, { value: "port", text: "A port number" }]
            value: dialog.what
            onPicked: v => dialog.what = v
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.what === "app"
        spacing: 6

        Repeater {
            model: dialog.apps

            FirewallChoice {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                title: modelData.name
                description: "Waiting for connections on " + (modelData.proto === "udp" ? "UDP port " : "port ") + modelData.port
                checked: dialog.appIndex === index
                onClicked: dialog.appIndex = index
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: dialog.apps.length === 0
            text: "No apps are waiting for connections right now. Start the app first, or choose A port number."
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: dialog.what === "port"
        spacing: 12

        UI.TextField {
            id: portField
            Layout.preferredWidth: 160
            placeholderText: "Port, such as 8096"
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: 65535 }
            text: dialog.portText
            onTextEdited: dialog.portText = text
            onAccepted: dialog.submit()
            Accessible.name: "Port number"
        }

        UI.Label {
            Layout.fillWidth: true
            text: {
                const known = dialog.port > 0 ? Firewall.serviceName(dialog.port, "tcp") : "";
                return known.length > 0 ? "Usually " + known + ". TCP and UDP." : dialog.port > 0 ? "TCP and UDP." : "";
            }
            size: 12
            color: UI.Style.inkMuted
            elide: Text.ElideRight
        }
    }

    UI.Label {
        Layout.fillWidth: true
        text: "WHO CAN CONNECT"
        size: 12.5
        font.weight: UI.Style.weightBold
        font.letterSpacing: 0.25
        color: UI.Style.inkSubtle
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        FirewallChoice {
            Layout.fillWidth: true
            title: "Devices on this network"
            description: dialog.networkName.length > 0 ? "Phones, TVs and PCs on " + dialog.networkName + ". Recommended." : "Phones, TVs and PCs on the network you're on. Recommended."
            checked: dialog.scope === "network"
            onClicked: dialog.scope = "network"
        }

        FirewallChoice {
            id: deviceChoice
            Layout.fillWidth: true
            title: "One device only"
            description: dialog.scope === "device" && dialog.address.length > 0 && dialog.addressProblem.length > 0
                ? dialog.addressProblem : "Give its address, such as 192.168.1.24."
            checked: dialog.scope === "device"
            showBelow: checked
            onClicked: {
                dialog.scope = "device";
                addressField.forceActiveFocus();
            }

            below: UI.TextField {
                id: addressField
                Layout.preferredWidth: 220
                placeholderText: "192.168.1.24"
                text: dialog.address
                error: dialog.address.length > 0 && dialog.addressProblem.length > 0
                onTextEdited: dialog.address = text.trim()
                onAccepted: dialog.submit()
                Accessible.name: "The device's address"
            }
        }

        FirewallChoice {
            Layout.fillWidth: true
            title: "Anyone, including the internet"
            description: "Only works if your router is set up to pass it on."
            checked: dialog.scope === "anyone"
            onClicked: dialog.scope = "anyone"

            PathIcon {
                name: "triangle-alert"
                size: 16
                strokeWidth: 2
                color: "#ffc46a"
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        UI.Label {
            Layout.fillWidth: true
            text: "FOR HOW LONG"
            size: 12.5
            font.weight: UI.Style.weightBold
            font.letterSpacing: 0.25
            color: UI.Style.inkSubtle
        }

        UI.ComboBox {
            id: durationBox
            Layout.preferredWidth: 220
            implicitHeight: 32
            Accessible.name: "For how long"
            model: [
                { value: "forever", text: "Until I turn it off" },
                { value: "restart", text: "Until I restart" },
                { value: "hour", text: "For one hour" }
            ]
            currentIndex: model.findIndex(o => o.value === dialog.duration)
            onActivated: dialog.duration = currentValue
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        PathIcon {
            name: "lock-round"
            size: 14
            strokeWidth: 2
            color: UI.Style.inkSubtle
        }

        UI.Label {
            Layout.fillWidth: true
            text: dialog.error.length > 0 ? dialog.error : "Never applies on a public network. You will be asked for your password."
            size: 12.5
            color: dialog.error.length > 0 ? UI.Style.warning : UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    buttons: [
        UI.Button {
            implicitHeight: 32
            text: "Cancel"
            onClicked: dialog.close()
        },
        UI.Button {
            implicitHeight: 32
            kind: "primary"
            text: Firewall.busy && Firewall.busyWhat === "allow" ? "Allowing" : "Allow"
            busy: Firewall.busy && Firewall.busyWhat === "allow"
            enabled: dialog.ready && !Firewall.busy
            onClicked: dialog.submit()
        }
    ]
}
