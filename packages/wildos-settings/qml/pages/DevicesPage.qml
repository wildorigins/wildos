import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// System > Devices (SettingsDevices.dc), Wild OS's Device Manager. What
// needs you comes first, each with the one button that fixes it, then
// everything that works. Devices finds the hardware; printers come from
// Printers (the queues, and those on the network not set up yet) and the
// fingerprint reader's state from Fingerprint. A device opens
// DeviceDetailDialog.
PageScroll {
    id: page

    property bool showAll: false
    property bool searchHandedOver: false
    readonly property int shortList: 7

    readonly property var usbQueues: Printers.printers.filter(p => /^(usb|ipp:\/\/localhost|ipps?:\/\/\[?::1)/.test(p.uri || ""))

    function printerRows() {
        const rows = [];
        for (const p of Printers.printers) {
            rows.push({ key: "queue:" + p.name, kind: "printer", name: p.info || p.name, icon: "printer-round",
                line: p.status || "Ready", state: "ok", badge: "", problem: "", action: "", actionLabel: "",
                model: p.model || "", maker: "", driver: "CUPS", hardwareId: "",
                connection: /^usb:/.test(p.uri || "") ? "USB" : /^(ipp:\/\/localhost|ipps?:\/\/\[?::1)/.test(p.uri || "") ? "USB, through ipp-usb" : "Your network",
                extra: p.location ? [{ label: "Location", value: p.location }] : [] });
        }
        // A search that started before the queues were read lists a printer
        // already set up as found; its queue says otherwise.
        const queued = Printers.printers.map(p => p.uri).concat(Printers.printers.map(p => p.info), Printers.printers.map(p => p.name));
        for (const f of Printers.found.filter(f => !queued.includes(f.uri) && !queued.includes(f.name))) {
            rows.push({ key: "found:" + f.uri, kind: "printer", name: f.name, icon: "printer-round",
                line: "", state: "attention", badge: "Not set up", problem: "Found on your network, but not set up yet.",
                action: "printers", actionLabel: "Set up", model: f.model || "", maker: "", driver: "", hardwareId: "",
                connection: "Your network", extra: f.location ? [{ label: "Location", value: f.location }] : [] });
        }
        return rows;
    }

    // Devices' list with what QML knows better: the fingerprints saved,
    // whether a USB printer has a queue, and the printers.
    readonly property var rows: {
        const out = [];
        for (const d of Devices.devices) {
            const row = Object.assign({}, d);
            if (row.kind === "fingerprint" && row.state === "ok") {
                const n = Fingerprint.fingers.length;
                row.line = Fingerprint.windowsKey ? "Holds a key from Windows Hello" : n === 0 ? "No fingers saved yet" : Format.plural(n, "finger", "fingers") + " saved";
            }
            if (row.kind === "printer" && row.state === "ok") {
                if (page.usbQueues.length === 0) {
                    row.state = "attention";
                    row.badge = "Not set up";
                    row.problem = "Plugged in, but not set up yet.";
                    row.action = "printers";
                    row.actionLabel = "Set up";
                } else {
                    // Its queue's row says the rest.
                    continue;
                }
            }
            out.push(row);
        }
        const printers = page.printerRows();
        const at = out.findIndex(r => ["scanner", "smartcard", "phone", "usbdrive", "card", "hdmisound", "cardreader", "usb", "usbctl", "storagectl", "firewire"].includes(r.kind));
        out.splice(at < 0 ? out.length : at, 0, ...printers);
        return out;
    }
    readonly property var attention: rows.filter(r => r.state === "attention")
    readonly property var working: rows.filter(r => r.state !== "attention")
    readonly property var shownWorking: showAll ? working : working.slice(0, shortList)

    function act(row) {
        if (row.action === "printers") {
            // Printers and scanners carries on with the search started here.
            page.searchHandedOver = true;
            Navigator.request = "printers-add";
            Navigator.open("printers");
        } else if (row.action === "graphicsdriver" || row.action === "update") {
            Navigator.open(row.action);
        } else if (row.action === "backup") {
            if (!Devices.openBackup()) {
                notice.show("Backup isn't installed", "Install it from the Store to copy your files to another drive.", true);
            }
        } else if (row.action === "restart") {
            Updates.restart();
        } else if (row.action === "allow") {
            Devices.allow(row.port);
        } else if (row.action === "report") {
            if (!Devices.openReport()) {
                notice.show("Report a problem isn't installed", "Install it from the Store to send a report.", true);
            }
        }
    }

    function checkAgain() {
        Devices.scan();
        Printers.reload();
        Printers.search();
        Fingerprint.reload();
    }

    Component.onCompleted: {
        if (Devices.ready) {
            Devices.scan();
        }
        Printers.search();
    }
    Component.onDestruction: {
        if (!page.searchHandedOver) {
            Printers.stopSearch();
        }
    }

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    Connections {
        target: Devices
        function onFailed(message) {
            notice.show("The device wasn't allowed", message, true);
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        // The summary: how many devices need you, or that none do.
        Rectangle {
            id: hero
            readonly property bool worry: page.attention.length > 0
            Layout.fillWidth: true
            implicitHeight: heroRow.implicitHeight + 28
            radius: 12
            color: UI.Style.tone(0.05)
            border.width: 1
            border.color: UI.Style.tone(0.07)

            RowLayout {
                id: heroRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 18

                Rectangle {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    radius: 23
                    color: Qt.rgba(heroIcon.color.r, heroIcon.color.g, heroIcon.color.b, 0.16)

                    PathIcon {
                        id: heroIcon
                        anchors.centerIn: parent
                        name: hero.worry ? "triangle-alert" : "status-ok"
                        size: 24
                        strokeWidth: 2.1
                        color: hero.worry ? (UI.Style.dark ? "#ffc46a" : UI.Style.warning) : (UI.Style.dark ? "#5fd38d" : "#1d7a4c")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    UI.Label {
                        Layout.fillWidth: true
                        text: !Devices.ready ? "Looking at the devices in this PC"
                            : hero.worry ? Format.plural(page.attention.length, "device needs", "devices need") + " you"
                            : "Every device is working"
                        size: 16.5
                        font.weight: UI.Style.weightBold
                        wrapMode: Text.Wrap
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: !Devices.ready ? "This takes a moment."
                            : (hero.worry ? "Everything else in this PC is working." : "Nothing in this PC needs you.")
                              + " Select a device to see its details and driver."
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                UI.Button {
                    implicitHeight: 32
                    iconName: "refresh"
                    text: "Check again"
                    busy: Devices.scanning
                    enabled: !Devices.scanning
                    onClicked: page.checkAgain()
                }
            }
        }

        SectionTitle {
            Layout.topMargin: 4
            Layout.bottomMargin: -4
            Layout.fillWidth: true
            visible: page.attention.length > 0
            text: "Needs attention"
        }

        DeviceList {
            Layout.fillWidth: true
            visible: page.attention.length > 0
            model: page.attention
            onOpened: row => details.openFor(row)
            onActed: row => page.act(row)
        }

        RowLayout {
            Layout.topMargin: 4
            Layout.bottomMargin: -4
            Layout.fillWidth: true
            visible: page.working.length > 0

            SectionTitle {
                Layout.fillWidth: true
                text: "Working"
            }

            UI.LinkButton {
                visible: page.working.length > page.shortList
                text: page.showAll ? "Show fewer" : "Show all " + page.working.length
                onClicked: page.showAll = !page.showAll
            }
        }

        DeviceList {
            Layout.fillWidth: true
            visible: page.working.length > 0
            model: page.shownWorking
            onOpened: row => details.openFor(row)
            onActed: row => page.act(row)
        }
    }

    DeviceDetailDialog {
        id: details
        onAct: row => page.act(row)
    }
}
