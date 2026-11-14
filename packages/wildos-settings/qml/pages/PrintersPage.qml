import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Printers and scanners (SettingsPrinters.dc, board 6.23). Opening the page
// looks for printers and scanners on the network; each one not added yet is
// offered at the top with one Add, which for a driverless printer (IPP
// Everywhere, AirPrint, Mopria) or an eSCL or WSD scanner is all it takes.
// Below, the printers and scanners already added, each with its state,
// whether it is the default, whether it also scans and its ink where the
// printer reports it; then the four ways out when something isn't working.
// Manage opens a printer's or scanner's own page, which stacks in the
// breadcrumb the way Windows does.
PageScroll {
    id: page

    property string selected
    readonly property var printer: {
        for (const p of Printers.printers) {
            if (p.name === selected) {
                return p;
            }
        }
        return null;
    }
    // A scanner's row, kept whole: USB ones come from Devices and have no
    // name of their own in Printers.
    property var scanner: null
    property string addingUri

    readonly property int depth: selected.length > 0 || scanner !== null ? 2 : 1
    readonly property var subTitles: printer ? [printer.info] : scanner ? [scanner.title] : []

    readonly property bool online: NetworkSettings.current.uuid !== undefined

    // Printers found on the network that are not set up. A search that
    // started before the queues were read can list one already added; its
    // queue says otherwise.
    readonly property var offers: {
        const queued = Printers.printers.map(p => p.uri).concat(Printers.printers.map(p => p.info), Printers.printers.map(p => p.name));
        const out = Printers.found.filter(f => !queued.includes(f.uri) && !queued.includes(f.name))
            .map(f => ({ kind: "printer", name: f.name, uri: f.uri, scans: f.scans }));
        for (const s of Printers.foundScanners) {
            out.push({ kind: "scanner", name: s.name, uri: "", scans: true });
        }
        return out;
    }

    readonly property var usbScanners: Devices.devices.filter(d => d.kind === "scanner")

    readonly property var rows: {
        const out = [];
        // The default first, as the board lists it, then by name.
        const printers = Printers.printers.slice().sort((a, b) => (b.isDefault - a.isDefault) || a.info.localeCompare(b.info));
        for (const p of printers) {
            out.push({ kind: "printer", key: p.name, title: p.info, line: statusOf(p), problem: p.problem });
        }
        for (const s of Printers.scanners) {
            out.push({ kind: "scanner", key: "net:" + s.name, title: s.name, url: s.url, network: true,
                line: s.seen ? "Ready · On your network" : Printers.scannersLooked ? "Not found on your network" : "On your network",
                problem: !s.seen && Printers.scannersLooked });
        }
        for (const d of usbScanners) {
            out.push({ kind: "scanner", key: "usb:" + d.name, title: d.name, url: "", network: false, model: d.model || "",
                line: "Ready · Connected by USB", problem: false });
        }
        return out;
    }

    // The printer "Print a test page" uses: the default, or the only one.
    readonly property var testPrinter: {
        const list = Printers.printers;
        return list.find(p => p.isDefault) || (list.length > 0 ? list[0] : null);
    }

    function back() {
        if (depth > 1) {
            selected = "";
            scanner = null;
            return true;
        }
        return false;
    }

    function statusOf(p) {
        return [p.status, p.isDefault ? "Default" : "", p.scans ? "Prints and scans" : "", p.ink,
                p.jobs > 0 ? Format.plural(p.jobs, "document", "documents") + " waiting" : ""]
            .filter(t => t && t.length > 0).join(" · ");
    }

    function offerText(o) {
        return (o.kind === "scanner" ? "Scans." : o.scans ? "Prints and scans." : "Prints.")
            + " It works straight away, with no driver to install.";
    }

    function openCapture() {
        if (!Printers.openCapture()) {
            notice.show("Capture didn't open", "Capture isn't installed on this computer.", true);
        }
    }

    Component.onDestruction: Printers.stopSearch()

    // The page looks as soon as it opens. System > Devices' "Set up" opens
    // it with Navigator.request "printers-add", which this look answers.
    Component.onCompleted: {
        if (Navigator.request === "printers-add") {
            Navigator.request = "";
        }
        Printers.search();
    }

    // Printers change state on their own (paper runs out, a job finishes).
    Timer {
        interval: 4000
        running: Printers.available && page.visible
        repeat: true
        onTriggered: Printers.reload()
    }

    Connections {
        target: Printers
        function onDone(title, message) {
            notice.show(title, message, false);
        }
        // A printer removed here, or anywhere else, leaves its page once the
        // list read after the change no longer has it.
        function onChanged() {
            if (page.selected.length > 0 && page.printer === null) {
                page.selected = "";
            }
        }
        function onFailed(title, reason) {
            notice.show(title, reason, true);
        }
    }

    // Joining a network is when its printers can be found.
    Connections {
        target: NetworkSettings
        function onCurrentChanged() {
            if (page.online && !Printers.searching) {
                Printers.search();
            }
        }
    }

    Notice {
        id: notice
    }

    StatusCard {
        visible: !Printers.available
        iconName: "printer"
        warning: true
        title: "Printing isn't running"
        description: "The service that talks to printers is not running on this computer, so printers can't be added or used."
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.depth === 1 && Printers.available
        spacing: Metrics.sectionGap

        // Several found at once read as one list, closer than sections.
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.offers.length > 0
            spacing: 10

            Repeater {
                model: page.offers

                OfferCard {
                    id: offer
                    required property var modelData
                    iconName: modelData.kind === "scanner" ? "scan-line" : "printer-round"
                    title: "Found on your network: " + modelData.name
                    description: page.offerText(modelData)

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Add"
                        kind: "primary"
                        busy: Printers.busy === "add" && page.addingUri === offer.modelData.uri && offer.modelData.kind === "printer"
                        enabled: Printers.busy.length === 0
                        onClicked: {
                            if (offer.modelData.kind === "scanner") {
                                Printers.addScanner(offer.modelData.name);
                            } else {
                                page.addingUri = offer.modelData.uri;
                                Printers.add(offer.modelData.uri, offer.modelData.name);
                            }
                        }
                    }
                }
            }
        }

        // Nothing to offer: still looking, nothing there, or no network to
        // look on. With printers already added, finding nothing new needs
        // no card of its own, as on the board.
        OfferCard {
            id: idle
            visible: page.offers.length === 0 && (page.rows.length === 0 || !page.online)
            readonly property bool looking: Printers.searching || Printers.findingScanners
            iconName: !page.online && !looking ? "wifi-round" : looking ? "search-round" : "printer-round"
            title: !page.online && !looking ? "Not connected to a network"
                : looking ? "Looking for printers and scanners"
                : "No new printers or scanners found"
            description: !page.online && !looking ? "Printers and scanners on a network show up here once this computer joins it."
                : looking ? "On the network this computer is joined to."
                : "Check it's switched on and joined to the same network as this computer."

            UI.Spinner {
                anchors.verticalCenter: parent.verticalCenter
                visible: idle.looking
            }

            UI.Button {
                anchors.verticalCenter: parent.verticalCenter
                visible: !idle.looking
                text: page.online ? "Search again" : "Network settings"
                onClicked: page.online ? Printers.search() : Navigator.open("network")
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: "Your printers and scanners"
            }

            UI.Card {
                Layout.fillWidth: true
                visible: page.rows.length > 0

                Column {
                    width: parent.width

                    Repeater {
                        model: page.rows

                        UI.SettingRow {
                            id: row
                            required property var modelData
                            title: modelData.title
                            description: modelData.line
                            descriptionColor: modelData.problem ? UI.Style.warning : UI.Style.inkMuted
                            interactive: true
                            minHeight: 60
                            Accessible.name: modelData.title + ", " + modelData.line
                            onClicked: manage()

                            function manage() {
                                if (modelData.kind === "printer") {
                                    page.selected = modelData.key;
                                } else {
                                    page.scanner = modelData;
                                }
                            }

                            leading: PathIcon {
                                name: row.modelData.kind === "scanner" ? "scan-line" : "printer-round"
                                size: 20
                                strokeWidth: 1.8
                            }

                            UI.LinkButton {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Manage"
                                Accessible.name: "Manage " + row.modelData.title
                                onClicked: row.manage()
                            }
                        }
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: page.rows.length === 0
                text: "No printers or scanners yet. Most made since 2014 work without a driver and show up above when they're switched on."
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: "If something is not working"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        title: "My printer is not in the list"
                        description: "Add it by its network address, or plug it in with a USB cable."
                        minHeight: 60
                        leading: PathIcon { name: "search-round"; size: 20; strokeWidth: 1.8 }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Add by address"
                            enabled: Printers.busy.length === 0
                            onClicked: addressDialog.open()
                        }
                    }

                    // Printing through Windows mode (board 6.24) waits on a
                    // test with real hardware. Until then this leads to the
                    // drivers Wild OS has, where a model from the same
                    // series often prints.
                    UI.SettingRow {
                        title: "It only has a Windows driver"
                        description: "Try a driver for the same maker or series. Plain printing works on many models."
                        minHeight: 60
                        visible: Pages.exists("printersadvanced")
                        leading: PathIcon { name: "app-window"; size: 20; strokeWidth: 1.8 }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Set up"
                            onClicked: {
                                Navigator.request = "printers-driver";
                                Navigator.open("printersadvanced");
                            }
                        }
                    }

                    UI.SettingRow {
                        title: "Print a test page"
                        description: page.testPrinter === null ? "Add a printer first."
                            : Printers.printers.length > 1 ? "Prints on " + page.testPrinter.info + ". Check it's lined up and has ink."
                            : "Check the printer is lined up and has ink."
                        minHeight: 60
                        leading: PathIcon { name: "file-text"; size: 20; strokeWidth: 1.8 }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Print"
                            busy: page.testPrinter !== null && Printers.busy === page.testPrinter.name
                            enabled: Printers.busy.length === 0 && page.testPrinter !== null && !page.testPrinter.paused
                            onClicked: Printers.printTestPage(page.testPrinter.name)
                        }
                    }

                    UI.SettingRow {
                        title: "Scan something"
                        description: "Opens Capture on its Scan tab."
                        minHeight: 60
                        leading: PathIcon { name: "scan-line"; size: 20; strokeWidth: 1.8 }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Open Capture"
                            onClicked: page.openCapture()
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: Pages.exists("printersadvanced")
            spacing: 10

            SectionTitle {
                text: "Related settings"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        iconName: "cog"
                        title: "More printer settings"
                        description: "Printers that need their own driver, USB printers and print queues"
                        chevron: true
                        onClicked: Navigator.open("printersadvanced")
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.printer !== null
        spacing: Metrics.sectionGap

        DeviceHeader {
            iconName: "printer-round"
            title: page.printer ? page.printer.info : ""
            line: page.printer ? page.statusOf(page.printer) : ""
            problem: page.printer !== null && page.printer.problem
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "check"
                    title: "Default printer"
                    description: page.printer && page.printer.isDefault ? "Apps print here unless you choose another printer"
                        : "Make apps print here unless you choose another printer"

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.printer !== null && !page.printer.isDefault
                        text: "Set as default"
                        onClicked: Printers.setDefault(page.selected)
                    }

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.printer !== null && page.printer.isDefault
                        text: "Default"
                        size: 12
                        color: UI.Style.inkMuted
                    }
                }

                UI.SettingRow {
                    iconName: "document"
                    title: "Print a test page"
                    description: "Checks the printer and this computer can talk to each other"

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Print test page"
                        busy: Printers.busy === page.selected
                        enabled: Printers.busy.length === 0 && page.printer !== null && !page.printer.paused
                        onClicked: Printers.printTestPage(page.selected)
                    }
                }

                UI.SettingRow {
                    visible: page.printer !== null && page.printer.scans
                    title: "Scan something"
                    description: "Opens Capture on its Scan tab"
                    leading: PathIcon { name: "scan-line"; size: 20; strokeWidth: 1.8 }

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Open Capture"
                        onClicked: page.openCapture()
                    }
                }

                UI.SettingRow {
                    visible: page.printer !== null && page.printer.jobs > 0
                    iconName: "list"
                    title: "Documents waiting"
                    description: page.printer ? Format.plural(page.printer.jobs, "document is", "documents are") + " waiting to print" : ""

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Cancel all"
                        onClicked: Printers.cancelJobs(page.selected)
                    }
                }

                UI.SettingRow {
                    visible: page.printer !== null && page.printer.paused
                    iconName: "warning"
                    title: "Printing is paused"
                    description: "Documents wait until the printer is resumed"

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Resume"
                        busy: Printers.busy === page.selected
                        enabled: Printers.busy.length === 0
                        onClicked: Printers.resume(page.selected)
                    }
                }

                UI.SettingRow {
                    iconName: "delete"
                    title: "Remove this printer"
                    description: "Apps stop offering it. You can add it again at any time."

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Remove"
                        danger: true
                        enabled: Printers.busy.length === 0
                        onClicked: removeDialog.open()
                    }
                }
            }
        }

        AboutCard {
            heading: "About this printer"
            facts: page.printer ? [
                ["Ink and toner", page.printer.inkDetail],
                ["Model", page.printer.model],
                ["Location", page.printer.location],
                ["Address", page.printer.uri],
                ["Name apps use", page.printer.name]
            ] : []
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.scanner !== null
        spacing: Metrics.sectionGap

        DeviceHeader {
            iconName: "scan-line"
            title: page.scanner ? page.scanner.title : ""
            line: page.scanner ? page.scanner.line : ""
            problem: page.scanner !== null && page.scanner.problem
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: "Scan something"
                    description: "Opens Capture on its Scan tab"
                    leading: PathIcon { name: "scan-line"; size: 20; strokeWidth: 1.8 }

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Open Capture"
                        onClicked: page.openCapture()
                    }
                }

                UI.SettingRow {
                    visible: page.scanner !== null && page.scanner.network
                    iconName: "delete"
                    title: "Remove this scanner"
                    description: "It leaves this list. Capture can still find it on the network."

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Remove"
                        danger: true
                        onClicked: {
                            Printers.removeScanner(page.scanner.title);
                            page.scanner = null;
                        }
                    }
                }
            }
        }

        AboutCard {
            heading: "About this scanner"
            facts: page.scanner ? [
                ["Connected by", page.scanner.network ? "Your network" : "USB"],
                ["Model", page.scanner.model || ""],
                ["Address", page.scanner.url || ""]
            ] : []
        }
    }

    // The tinted circle and two lines of the board's "Found on your
    // network" card, in the list cards' own surface.
    component OfferCard: UI.Card {
        id: offerCard

        property string iconName
        property string title
        property string description
        default property alias actions: actionRow.data

        Layout.fillWidth: true
        topPadding: 14
        bottomPadding: 14
        leftPadding: 18
        rightPadding: 18

        RowLayout {
            width: parent.width
            spacing: 18

            Rectangle {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                radius: 23
                color: Qt.rgba(UI.Style.accent.r, UI.Style.accent.g, UI.Style.accent.b, UI.Style.dark ? 0.16 : 0.12)

                PathIcon {
                    anchors.centerIn: parent
                    name: offerCard.iconName
                    size: 24
                    strokeWidth: 2.1
                    color: UI.Style.accent
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                UI.Label {
                    Layout.fillWidth: true
                    text: offerCard.title
                    size: 16.5
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: offerCard.description
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }

            Row {
                id: actionRow
                Layout.alignment: Qt.AlignVCenter
                spacing: 8
            }
        }
    }

    component DeviceHeader: UI.Card {
        id: header

        property string iconName
        property string title
        property string line
        property bool problem: false

        Layout.fillWidth: true
        implicitHeight: 96

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 18

            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: UI.Style.radiusMd
                color: UI.Style.surfaceTile

                PathIcon {
                    anchors.centerIn: parent
                    name: header.iconName
                    size: 28
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: header.title
                    size: 18
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: header.line
                    color: header.problem ? UI.Style.warning : UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }
        }
    }

    component AboutCard: ColumnLayout {
        id: about

        property string heading
        property var facts: []

        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: about.heading
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: about.facts.filter(r => r[1] && r[1].length > 0)

                    UI.SettingRow {
                        required property var modelData
                        title: modelData[0]

                        UI.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, 420)
                            text: modelData[1]
                            color: UI.Style.inkMuted
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }

    UI.Dialog {
        id: addressDialog
        title: "Add a printer by its address"
        text: "Type the printer's network name or IP address. You can usually find it on the printer's screen or a page it prints about its network settings."
        onAboutToShow: {
            addressField.text = "";
            addressField.forceActiveFocus();
        }

        UI.TextField {
            id: addressField
            Layout.fillWidth: true
            placeholderText: "For example 192.168.1.20 or printer.local"
            onAccepted: addressAdd.clicked()
        }

        buttons: [
            UI.Button {
                id: addressAdd
                text: "Add"
                kind: "primary"
                enabled: addressField.text.trim().length > 0
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    addressDialog.close();
                    page.addingUri = "";
                    Printers.addAddress(addressField.text);
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: addressDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: removeDialog
        title: page.printer ? "Remove " + page.printer.info + "?" : ""
        text: "Documents waiting for it are cancelled. You can add the printer again at any time."
        buttons: [
            UI.Button {
                text: "Remove"
                kind: "primary"
                onClicked: {
                    removeDialog.close();
                    Printers.remove(page.selected);
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: removeDialog.close()
            }
        ]
    }
}
