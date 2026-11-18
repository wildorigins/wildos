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
                line: p.status || qsTr("Ready"), state: "ok", badge: "", problem: "", action: "", actionLabel: "",
                model: p.model || "", maker: "", driver: "CUPS", hardwareId: "",
                connection: /^usb:/.test(p.uri || "") ? "USB" : /^(ipp:\/\/localhost|ipps?:\/\/\[?::1)/.test(p.uri || "") ? qsTr("USB, through ipp-usb") : qsTr("Your network"),
                extra: p.location ? [{ label: qsTr("Location"), value: p.location }] : [] });
        }
        // A search that started before the queues were read lists a printer
        // already set up as found; its queue says otherwise.
        const queued = Printers.printers.map(p => p.uri).concat(Printers.printers.map(p => p.info), Printers.printers.map(p => p.name));
        for (const f of Printers.found.filter(f => !queued.includes(f.uri) && !queued.includes(f.name))) {
            rows.push({ key: "found:" + f.uri, kind: "printer", name: f.name, icon: "printer-round",
                line: "", state: "attention", badge: qsTr("Not set up"), problem: qsTr("Found on your network, but not set up yet."),
                action: "printers", actionLabel: qsTr("Set up"), model: f.model || "", maker: "", driver: "", hardwareId: "",
                connection: qsTr("Your network"), extra: f.location ? [{ label: qsTr("Location"), value: f.location }] : [] });
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
                row.line = Fingerprint.windowsKey ? qsTr("Holds a key from Windows Hello") : n === 0 ? qsTr("No fingers saved yet") : n === 1 ? qsTr("1 finger saved") : qsTr("%1 fingers saved").arg(n);
            }
            if (row.kind === "printer" && row.state === "ok") {
                if (page.usbQueues.length === 0) {
                    row.state = "attention";
                    row.badge = qsTr("Not set up");
                    row.problem = qsTr("Plugged in, but not set up yet.");
                    row.action = "printers";
                    row.actionLabel = qsTr("Set up");
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
                notice.show(qsTr("Backup isn't installed"), qsTr("Install it from the Store to copy your files to another drive."), true);
            }
        } else if (row.action === "restart") {
            Updates.restart();
        } else if (row.action === "allow") {
            Devices.allow(row.port);
        } else if (row.action === "report") {
            if (!Devices.openReport()) {
                notice.show(qsTr("Report a problem isn't installed"), qsTr("Install it from the Store to send a report."), true);
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
            notice.show(qsTr("The device wasn't allowed"), message, true);
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
                        color: hero.worry ? UI.Style.warning : UI.Style.success
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    UI.Label {
                        Layout.fillWidth: true
                        text: !Devices.ready ? qsTr("Looking at the devices in this PC")
                            : hero.worry ? (page.attention.length === 1 ? qsTr("1 device needs you") : qsTr("%1 devices need you").arg(page.attention.length))
                            : qsTr("Every device is working")
                        size: 16.5
                        font.weight: UI.Style.weightBold
                        wrapMode: Text.Wrap
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: !Devices.ready ? qsTr("This takes a moment.")
                            : hero.worry ? qsTr("Everything else in this PC is working. Select a device to see its details and driver.")
                            : qsTr("Nothing in this PC needs you. Select a device to see its details and driver.")
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                UI.Button {
                    implicitHeight: 32
                    iconName: "refresh"
                    text: qsTr("Check again")
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
            text: qsTr("Needs attention")
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
                text: qsTr("Working")
            }

            UI.LinkButton {
                visible: page.working.length > page.shortList
                text: page.showAll ? qsTr("Show fewer") : qsTr("Show all %1").arg(page.working.length)
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
