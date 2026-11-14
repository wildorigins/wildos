import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// More printer settings: what Printers and scanners leaves out. Each
// printer opens its print queue, with Cancel per document and Pause, and
// its printing preferences (paper size, two-sided, colour), kept for this
// account. "Add a printer that needs a driver" covers USB printers and
// older network printers: choose the printer, then its maker and model from
// the drivers installed.
PageScroll {
    id: page

    property string selected
    property bool adding: false
    readonly property var printer: {
        for (const p of Printers.printers) {
            if (p.name === selected) {
                return p;
            }
        }
        return null;
    }
    property var prefs: ({})

    readonly property int depth: selected.length > 0 || adding ? 2 : 1
    readonly property var subTitles: adding ? ["Add a printer that needs a driver"] : printer ? [printer.info] : []

    function back() {
        if (selected.length > 0 || adding) {
            selected = "";
            adding = false;
            return true;
        }
        return false;
    }

    function statusOf(p) {
        return (p.isDefault ? "Default, " + p.status.charAt(0).toLowerCase() + p.status.slice(1) : p.status)
            + (p.jobs > 0 ? ", " + Format.plural(p.jobs, "document", "documents") + " waiting" : "");
    }

    function startAdding() {
        nameField.text = "";
        addressField.text = "";
        adding = true;
        Printers.findDevices();
        Printers.loadDrivers();
    }

    onSelectedChanged: prefs = selected.length > 0 ? Printers.preferences(selected) : ({})

    // Printers and scanners' "It only has a Windows driver" opens this page
    // with Navigator.request "printers-driver", straight into choosing a
    // driver.
    Component.onCompleted: {
        if (Navigator.request === "printers-driver") {
            Navigator.request = "";
            startAdding();
        }
    }

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
            if (title === "Printer added") {
                page.adding = false;
            }
        }
        function onFailed(title, reason) {
            notice.show(title, reason, true);
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
        visible: Printers.available && page.depth === 1
        spacing: Metrics.sectionGap

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "printer"
                    iconTile: true
                    minHeight: 68
                    title: "Add a printer that needs a driver"
                    description: "For printers plugged in with USB, and older network printers"

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Add a printer"
                        kind: "primary"
                        enabled: Printers.busy.length === 0
                        onClicked: page.startAdding()
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: Printers.printers.length > 0
            spacing: 10

            SectionTitle {
                text: "Print queues and preferences"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    Repeater {
                        model: Printers.printers

                        UI.SettingRow {
                            required property var modelData
                            iconName: "printer"
                            title: modelData.info
                            description: page.statusOf(modelData)
                            descriptionColor: modelData.problem ? UI.Style.warning : UI.Style.inkMuted
                            chevron: true
                            onClicked: page.selected = modelData.name
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.printer !== null && !page.adding
        spacing: Metrics.sectionGap

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "printer"
                    iconTile: true
                    minHeight: 68
                    title: page.printer ? page.printer.info : ""
                    description: page.printer ? page.statusOf(page.printer) : ""
                    descriptionColor: page.printer && page.printer.problem ? UI.Style.warning : UI.Style.inkMuted

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.printer !== null && !page.printer.isDefault
                        text: "Set as default"
                        onClicked: Printers.setDefault(page.selected)
                    }

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.printer && page.printer.paused ? "Resume printing" : "Pause printing"
                        busy: Printers.busy === page.selected
                        enabled: Printers.busy.length === 0
                        onClicked: page.printer.paused ? Printers.resume(page.selected) : Printers.pause(page.selected)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: "Print queue"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    Repeater {
                        model: page.printer ? page.printer.queue : []

                        UI.SettingRow {
                            id: job
                            required property var modelData
                            iconName: "document"
                            title: modelData.title.length > 0 ? modelData.title : "Document"
                            description: [modelData.state, modelData.user, Format.time(modelData.created)].filter(t => t && t.length > 0).join(", ")

                            UI.Button {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Cancel"
                                onClicked: Printers.cancelJob(page.selected, job.modelData.id)
                            }
                        }
                    }

                    UI.SettingRow {
                        visible: page.printer !== null && page.printer.queue.length === 0
                        iconName: "check"
                        title: "No documents waiting"
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: page.prefs.media !== undefined || page.prefs.sides !== undefined || page.prefs.color !== undefined
            spacing: 10

            SectionTitle {
                text: "Printing preferences"
            }

            UI.Label {
                Layout.fillWidth: true
                text: "What apps choose for this printer unless you change it when printing. Kept for your account only."
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    Repeater {
                        model: [
                            { key: "media", option: "media", icon: "document", title: "Paper size" },
                            { key: "sides", option: "sides", icon: "copy", title: "Two-sided printing" },
                            { key: "color", option: "print-color-mode", icon: "palette", title: "Colour" }
                        ].filter(r => page.prefs[r.key] !== undefined)

                        UI.SettingRow {
                            id: pref
                            required property var modelData
                            readonly property var setting: page.prefs[modelData.key]
                            iconName: modelData.icon
                            title: modelData.title

                            UI.ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: pref.modelData.title
                                model: pref.setting ? pref.setting.choices : []
                                currentIndex: pref.setting ? pref.setting.choices.findIndex(c => c.value === pref.setting.value) : -1
                                onActivated: index => {
                                    Printers.setPreference(page.selected, pref.modelData.option, model[index].value);
                                    page.prefs = Printers.preferences(page.selected);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: add
        Layout.fillWidth: true
        visible: page.adding && Printers.available
        spacing: Metrics.sectionGap

        // The device list plus a last choice for typing an address.
        readonly property var connections: Printers.devices.map(d => ({
            value: d.uri,
            text: (d.kind === "usb" ? "USB: " : "Network: ") + (d.name.length > 0 ? d.name : d.uri),
            device: d
        })).concat([{ value: "", text: "A network printer by its address", device: null }])
        readonly property var connection: connectionBox.currentIndex >= 0 && connectionBox.currentIndex < connections.length
            ? connections[connectionBox.currentIndex] : null
        readonly property bool byAddress: connection !== null && connection.device === null
        readonly property string uri: byAddress ? Printers.addressUri(addressField.text, protocolBox.currentValue || "socket")
            : connection ? connection.value : ""
        property var makeList: []
        property var modelList: []
        property string suggestedName

        function suggest() {
            const d = connection ? connection.device : null;
            const model = d ? d.model : "";
            if (model.length > 0) {
                const make = Printers.makeFor(model);
                const i = makeList.indexOf(make);
                if (i >= 0) {
                    makeBox.currentIndex = i;
                    modelList = Printers.models(make);
                    const m = modelList.findIndex(o => o.text.toLowerCase().startsWith(model.toLowerCase()));
                    modelBox.currentIndex = m >= 0 ? m : 0;
                }
            }
            // A name typed in is kept; one filled in from the device follows
            // the device.
            const name = d ? (d.name.length > 0 ? d.name : d.model) : addressField.text.trim();
            if (nameField.text.length === 0 || nameField.text === suggestedName) {
                nameField.text = name;
            }
            suggestedName = name;
        }

        Connections {
            target: Printers
            function onDriversChanged() {
                add.makeList = Printers.makes();
                add.modelList = add.makeList.length > 0 ? Printers.models(add.makeList[Math.max(0, makeBox.currentIndex)]) : [];
                add.suggest();
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: "Printer"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        iconName: "plug"
                        title: "Connected by"
                        description: Printers.findingDevices ? "Looking for printers" : ""

                        UI.Spinner {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: Printers.findingDevices
                        }

                        UI.ComboBox {
                            id: connectionBox
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: "Connected by"
                            enabled: !Printers.findingDevices
                            model: add.connections
                            onActivated: add.suggest()
                            onModelChanged: {
                                currentIndex = 0;
                                add.suggest();
                            }
                        }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Look again"
                            enabled: !Printers.findingDevices
                            onClicked: Printers.findDevices()
                        }
                    }

                    UI.SettingRow {
                        visible: add.byAddress
                        title: "Address"
                        description: "The printer's network name or IP address"

                        UI.TextField {
                            id: addressField
                            anchors.verticalCenter: parent.verticalCenter
                            width: 240
                            placeholderText: "For example 192.168.1.20"
                            onTextChanged: if (add.byAddress) add.suggest()
                        }

                        UI.ComboBox {
                            id: protocolBox
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: "How it prints"
                            model: [
                                { value: "socket", text: "Port 9100 (most printers)" },
                                { value: "lpd", text: "LPD" },
                                { value: "ipp", text: "IPP" }
                            ]
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: "Driver"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        iconName: "cog"
                        title: "Maker"
                        description: Printers.loadingDrivers ? "Reading the installed drivers, which can take a moment" : ""

                        UI.Spinner {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: Printers.loadingDrivers
                        }

                        UI.ComboBox {
                            id: makeBox
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: "Maker"
                            enabled: add.makeList.length > 0
                            model: add.makeList.map(m => ({ value: m, text: m }))
                            onActivated: index => {
                                add.modelList = Printers.models(add.makeList[index]);
                                modelBox.currentIndex = 0;
                            }
                        }
                    }

                    UI.SettingRow {
                        iconName: "printer"
                        title: "Model"
                        description: "Pick the exact model, or the nearest one from the same series"

                        UI.ComboBox {
                            id: modelBox
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, 420)
                            Accessible.name: "Model"
                            enabled: add.modelList.length > 0
                            model: add.modelList
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: "Name"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        iconName: "rename"
                        title: "Printer name"
                        description: "How apps show it when you print"

                        UI.TextField {
                            id: nameField
                            anchors.verticalCenter: parent.verticalCenter
                            width: 280
                            placeholderText: "For example Office printer"
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item {
                Layout.fillWidth: true
            }

            UI.Button {
                text: "Cancel"
                onClicked: page.adding = false
            }

            UI.Button {
                text: "Add printer"
                kind: "primary"
                busy: Printers.busy === "add"
                enabled: Printers.busy.length === 0 && add.uri.length > 0 && modelBox.currentIndex >= 0 && add.modelList.length > 0
                    && nameField.text.trim().length > 0
                onClicked: Printers.addWithDriver(add.uri, add.modelList[modelBox.currentIndex].value, nameField.text)
            }
        }
    }
}
