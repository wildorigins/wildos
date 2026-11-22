import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// Windows apps > Windows mode, laid out as WinModeSettings.dc. Every value
// is the Windows mode service's own (WindowsMode, src/windowsmode.h); when
// Windows mode is not installed or not set up the page says so and offers
// the one button that helps.
PageScroll {
    id: page

    readonly property var status: WindowsMode.status
    readonly property var licence: status.licence || ({})
    readonly property var options: status.options || ({})
    // Activating with a licence on a Microsoft account, from the agent:
    // state is starting, open, ending or "".
    readonly property var activation: status.activation || ({ state: "" })
    readonly property bool ready: WindowsMode.state === "ready"
    readonly property string vm: status.vm || ""
    property string message

    function show(result) {
        page.message = result || "";
    }

    UI.Card {
        Layout.fillWidth: true
        visible: !page.ready

        Column {
            width: parent.width

            UI.ListRow {
                title: !WindowsMode.installed ? qsTr("Windows mode isn't installed")
                     : WindowsMode.state === "setup" ? qsTr("Setting up Windows mode")
                     : WindowsMode.state === "failed" ? qsTr("Windows mode couldn't finish setting up")
                     : qsTr("Windows mode isn't set up")
                description: !WindowsMode.installed ? qsTr("Get Windows mode from the Store to run Windows programs like Office and Photoshop in their own windows.")
                     : WindowsMode.state === "setup" ? qsTr("%1% done.").arg(page.status.percent || 0) + " " + (page.status.left || "")
                     : WindowsMode.state === "failed" ? ((page.status.problem || {}).text || qsTr("Nothing was left half installed."))
                     : qsTr("Run Windows programs like Office and Photoshop in their own windows, using the Windows licence built into this PC.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: WindowsMode.installed
                    kind: "primary"
                    text: WindowsMode.state === "setup" ? qsTr("Show progress") : WindowsMode.state === "failed" ? qsTr("Try again") : qsTr("Set up")
                    onClicked: WindowsMode.openSetup()
                }

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !WindowsMode.installed && System.storeApp.length > 0
                    text: qsTr("Open the Store")
                    onClicked: Qt.openUrlExternally("appstream://org.wildos.winmode")
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: page.ready
        spacing: 12

        Repeater {
            model: [
                {
                    label: qsTr("Status"),
                    value: page.vm === "running" ? qsTr("Running") : page.vm === "paused" ? qsTr("Paused") : page.vm === "saved" ? qsTr("Saved to disk") : qsTr("Off"),
                    detail: page.vm === "running" ? qsTr("%1 Windows apps open").arg((page.status.apps || []).filter(a => a.open).length)
                          : page.vm === "paused" ? qsTr("Opens in about 1 second")
                          : page.vm === "saved" ? qsTr("Opens in about 5 seconds") : qsTr("Opens in about 30 seconds")
                },
                {
                    label: qsTr("Memory"),
                    value: page.status.memoryHeld > 0 ? qsTr("%1 held").arg(WindowsMode.size(page.status.memoryHeld)) : qsTr("None held"),
                    detail: page.vm === "paused" ? qsTr("No CPU while paused") : page.vm === "running" ? qsTr("Handed back when unused") : qsTr("Nothing in use")
                },
                {
                    label: qsTr("Space"),
                    value: WindowsMode.size(page.status.diskBytes) || qsTr("Unknown"),
                    detail: page.options.office ? qsTr("Windows and Microsoft 365") : "Windows"
                }
            ]

            UI.Card {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: tileColumn.implicitHeight + 28

                Column {
                    id: tileColumn
                    x: 16
                    y: 14
                    width: parent.width - 32
                    spacing: 4

                    UI.Label {
                        text: modelData.label
                        size: 12
                        color: UI.Style.inkMuted
                    }

                    UI.Label {
                        text: modelData.value
                        size: 18
                        font.weight: UI.Style.weightSemiBold
                    }

                    UI.Label {
                        text: modelData.detail
                        size: 12
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.ready

        Column {
            width: parent.width

            UI.ListRow {
                minHeight: 54
                title: qsTr("Licence")
                description: {
                    const edition = page.licence.edition === "home" ? "Windows 11 Home" : page.licence.edition === "pro" ? "Windows 11 Pro" : "Windows 11";
                    if (page.licence.source === "none") {
                        return qsTr("%1, not activated").arg(edition);
                    }
                    const how = page.licence.source === "key" ? qsTr("your product key ending %1").arg(page.licence.keyEnd || "")
                              : page.licence.source === "account" ? qsTr("the licence on your Microsoft account")
                              : qsTr("this PC's built-in licence");
                    return page.licence.activated === true ? qsTr("%1, activated with %2").arg(edition).arg(how) : qsTr("%1, to be activated with %2").arg(edition).arg(how);
                }

                UI.Chip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.licence.activated === true ? qsTr("Activated") : qsTr("Not activated")
                    tone: page.licence.activated === true ? "success" : "warning"
                }

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.licence.source === "none" ? qsTr("Add a key") : qsTr("Change key")
                    onClicked: keyDialog.open()
                }
            }

            UI.ListRow {
                minHeight: 54
                visible: page.licence.activated !== true || page.activation.state !== ""
                title: qsTr("Use a licence on my Microsoft account")
                description: page.activation.state !== ""
                    ? qsTr("Under way in the Activate Windows window.")
                    : qsTr("For Windows 11 Pro activated on another PC with your Microsoft account. Microsoft may let you move it here.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.activation.state !== "" ? qsTr("Show steps") : qsTr("Start")
                    onClicked: accountDialog.open()
                }
            }

            UI.ListRow {
                minHeight: 54
                title: qsTr("When no Windows app is open")
                description: {
                    switch (page.options.whenIdle) {
                    case "memory": return qsTr("Kept ready in memory, using no processor time");
                    case "disk": return qsTr("Saved to disk after a minute, so its memory is free");
                    case "shutdown": return qsTr("Windows shuts down after five minutes");
                    default: return qsTr("Automatic: kept in memory for 15 minutes, then saved to disk, at once on battery");
                    }
                }

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("When no Windows app is open")
                    model: [
                        { value: "automatic", text: qsTr("Automatic") },
                        { value: "memory", text: qsTr("Keep ready in memory") },
                        { value: "disk", text: qsTr("Save to disk") },
                        { value: "shutdown", text: qsTr("Shut down Windows") }
                    ]
                    currentIndex: model.findIndex(o => o.value === (page.options.whenIdle || "automatic"))
                    onActivated: index => page.show(WindowsMode.setOption("whenIdle", model[index].value))
                }
            }

            UI.ListRow {
                minHeight: 54
                title: qsTr("Memory Windows mode can use")
                // Windows only takes a new limit when it starts from off, so
                // until then the row says plainly that the change is waiting.
                description: page.status.memoryPending
                    ? qsTr("Windows gets %1 GB when it next starts. Wild OS restarts it by itself once no Windows app is open.").arg(page.options.memoryGb || 4)
                    : qsTr("Unused memory is handed back to Wild OS. A change applies once no Windows app is open, when Windows restarts.")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Memory limit")
                    model: WindowsMode.memoryChoices
                    currentIndex: model.findIndex(o => o.value === (page.options.memoryGb || 4))
                    onActivated: index => page.show(WindowsMode.setOption("memoryGb", String(model[index].value)))
                }
            }

            // A shortcut: the switch lives in System > Power and battery, because
            // it speeds up the whole PC, not only Windows mode.
            UI.ListRow {
                minHeight: 54
                visible: Processor.threadsAvailable
                title: qsTr("Use every processor thread")
                description: Processor.everyThreadLocked
                    ? qsTr("Off while extra processor protection is on.")
                    : qsTr("Faster Windows apps and games. Set in Power and battery.")
                status: Processor.everyThread && !Processor.everyThreadLocked ? qsTr("On") : qsTr("Off")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Change")
                    onClicked: Navigator.open("power")
                }
            }

            UI.ListRow {
                minHeight: 54
                title: qsTr("Folders Windows apps can see")
                description: {
                    const list = (page.options.folders || []).map(f => qsTr(f));
                    if (list.length === 0) {
                        return qsTr("None. Windows apps can't open or save your files.");
                    }
                    return list.length === 1 ? list[0] : qsTr("%1 and %2").arg(list.slice(0, -1).join(", ")).arg(list[list.length - 1]);
                }

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Change")
                    onClicked: foldersDialog.open()
                }
            }

            UI.ListRow {
                minHeight: 54
                title: qsTr("Camera and microphone")
                description: {
                    switch (page.options.devices) {
                    case "always": return qsTr("Windows apps always get them");
                    case "never": return qsTr("Windows apps never get them");
                    default: return qsTr("Ask each time a Windows app wants them, like any other app");
                    }
                }

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Camera and microphone")
                    model: [
                        { value: "ask", text: qsTr("Ask each time") },
                        { value: "always", text: qsTr("Always allow") },
                        { value: "never", text: qsTr("Never allow") }
                    ]
                    currentIndex: model.findIndex(o => o.value === (page.options.devices || "ask"))
                    onActivated: index => page.show(WindowsMode.setOption("devices", model[index].value))
                }
            }

            UI.ListRow {
                minHeight: 54
                title: qsTr("Windows and Office updates")
                description: qsTr("Monthly updates install a week after Microsoft releases them.")
                    + (page.status.officeVersion ? " Microsoft 365 " + page.status.officeVersion + "." : "")

            }
        }
    }

    // Devices that only work with Windows. The service (winmode/usb.py)
    // remembers each choice and hands the device over whenever Windows
    // runs; this lists what is plugged in and says plainly why a device
    // stays with Wild OS.
    ColumnLayout {
        id: usbSection
        Layout.fillWidth: true
        visible: page.ready
        spacing: 10

        onVisibleChanged: if (visible) WindowsUsb.reload()
        Component.onCompleted: if (visible) WindowsUsb.reload()

        function kindName(kind) {
            switch (kind) {
            case "printer": return qsTr("Printer");
            case "audio": return qsTr("Sound device");
            case "camera": return qsTr("Camera");
            case "scanner": return qsTr("Scanner or camera");
            case "storage": return qsTr("Drive");
            case "smartcard": return qsTr("Card reader");
            case "wireless": return qsTr("Wireless adapter");
            case "network": return qsTr("Network adapter");
            default: return qsTr("USB device");
            }
        }

        function icon(device) {
            if (device.problem === "keyboard") return "keyboard";
            if (device.problem === "pointer") return "mouse";
            switch (device.kind) {
            case "printer": case "scanner": return "printer";
            case "audio": return "sound";
            case "camera": return "camera";
            case "storage": return "drive";
            case "wireless": return "bluetooth";
            case "network": return "ethernet";
            default: return "usb";
            }
        }

        function describe(device) {
            switch (device.state) {
            case "windows": return qsTr("In use by Windows");
            case "blocked": return WindowsUsb.canAllow ? qsTr("Blocked by USB protection until you allow it")
                                                       : qsTr("Blocked by USB protection. Allow it from the shield on the taskbar.");
            case "refused":
                if (device.problem === "keyboard") return qsTr("You type into Wild OS with it, so it stays here");
                if (device.problem === "pointer") return qsTr("You move the pointer in Wild OS with it, so it stays here");
                return qsTr("Wild OS runs from this drive, so it stays here");
            case "waiting":
                if (device.problem === "in-use") return qsTr("Goes to Windows once you eject it in Files");
                if (device.problem === "full") return qsTr("Windows can't take any more devices. Switch one off first.");
                if (device.problem === "failed") return qsTr("Windows couldn't take it. Unplug it and plug it back in.");
                return WindowsUsb.running ? qsTr("Going to Windows") : qsTr("Goes to Windows when a Windows app opens");
            default:
                return device.problem === "in-use" ? qsTr("In use by Wild OS. Eject it in Files before Windows can have it.") : qsTr("Used by Wild OS");
            }
        }

        Connections {
            target: WindowsUsb
            function onFailed(message) {
                page.show(message);
            }
        }

        SectionTitle {
            text: qsTr("USB devices")
        }

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("For a device that only works with Windows, such as a licence dongle, label printer or scanner. Wild OS gets it back when you switch it off or Windows stops.")
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    id: usbList
                    model: WindowsUsb.devices

                    UI.ListRow {
                        id: usbRow
                        required property var modelData
                        readonly property bool problem: modelData.state === "blocked"
                            || modelData.problem === "full" || modelData.problem === "failed"
                        minHeight: 54
                        iconName: usbSection.icon(modelData)
                        title: modelData.name || usbSection.kindName(modelData.kind)
                        description: usbSection.describe(modelData)
                        descriptionColor: problem ? UI.Style.warning : UI.Style.inkMuted

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: usbRow.modelData.state === "blocked" && WindowsUsb.canAllow
                            text: qsTr("Allow once")
                            busy: WindowsUsb.busy === usbRow.modelData.port
                            enabled: WindowsUsb.busy.length === 0
                            Accessible.name: qsTr("Allow %1 once").arg(usbRow.title)
                            onClicked: WindowsUsb.allow(usbRow.modelData.port)
                        }

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: usbRow.modelData.state !== "blocked" || usbRow.modelData.use
                            text: qsTr("Use in Windows")
                            checked: usbRow.modelData.use
                            locked: usbRow.modelData.state === "refused"
                            lockedReason: usbRow.description
                            enabled: WindowsUsb.busy.length === 0
                            Accessible.name: qsTr("Use %1 in Windows").arg(usbRow.title)
                            onToggled: WindowsUsb.setUse(usbRow.modelData.port, checked)
                        }
                    }
                }

                UI.ListRow {
                    visible: WindowsUsb.loaded && usbList.count === 0
                    iconName: "usb"
                    title: qsTr("No USB devices plugged in")
                    description: qsTr("Plug in a device and it appears here.")
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.ready

        Column {
            width: parent.width

            UI.ListRow {
                minHeight: 54
                title: qsTr("Troubleshooting")
                description: qsTr("Open the whole Windows desktop in a window, for Windows' own settings or an installer that needs it. Your Windows apps join it until you close it.")

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    UI.Button {
                        text: qsTr("Windows desktop")
                        onClicked: page.show(WindowsMode.openDesktop())
                    }

                }
            }

            UI.ListRow {
                minHeight: 54
                title: qsTr("Windows password")
                description: qsTr("Windows mode signs in for you with a password of its own. Windows asks for it now and then, for activation or an installer that needs an administrator.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Show")
                    onClicked: passwordDialog.open()
                }
            }

            UI.ListRow {
                minHeight: 54
                title: qsTr("Remove Windows mode")
                description: WindowsMode.removing ? qsTr("Removing Windows. This can take a minute.")
                    : qsTr("Deletes Windows and the programs in it, to free the space or to set Windows mode up again from the start. Your documents stay in your folders.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Remove")
                    busy: WindowsMode.removing
                    enabled: !WindowsMode.removing
                    onClicked: removeDialog.open()
                }
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: page.message.length > 0
        text: page.message
        size: 13
        color: UI.Style.warning
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    // The dialog stays open while the key goes in, which can take a few
    // minutes when Windows has to start first, and says how it went.
    UI.Dialog {
        id: keyDialog
        title: qsTr("Change the Windows product key")
        text: qsTr("Windows checks the key with Microsoft and activates with it. The key from this PC's firmware stays where it is.")
        closePolicy: WindowsMode.activating ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape

        property string keyProblem

        onOpened: {
            keyField.text = "";
            keyProblem = "";
        }

        Connections {
            target: WindowsMode
            function onProductKeyDone(answer) {
                if (answer === "") {
                    keyDialog.close();
                    page.show("");
                } else {
                    keyDialog.keyProblem = answer;
                }
            }
        }

        UI.TextField {
            id: keyField
            Layout.fillWidth: true
            enabled: !WindowsMode.activating
            placeholderText: "XXXXX-XXXXX-XXXXX-XXXXX-XXXXX"
            // Room for a pasted key with spaces round its dashes; shaping
            // cuts it to 25 characters.
            maximumLength: 64
            error: text.length >= 25 && !WindowsMode.keyShapeValid(text)
            Accessible.name: qsTr("Windows product key")

            // The text after the last edit, to tell when an edit removed
            // nothing but a dash.
            property string shaped: ""
            property bool forwardDelete: false

            Keys.onDeletePressed: event => {
                forwardDelete = true;
                event.accepted = false;
                // The edit happens during this key press, so a Delete that removed
                // nothing must not count for a later Backspace.
                Qt.callLater(() => forwardDelete = false);
            }

            // Capitals and a dash after every five, as Windows' own key box
            // does, however the key is typed or pasted.
            onTextEdited: {
                let typed = text;
                let cursor = cursorPosition;
                if (typed.length === shaped.length - 1) {
                    let i = 0;
                    while (i < typed.length && typed[i] === shaped[i]) {
                        i++;
                    }
                    // The dash would only come straight back, so the
                    // character beside it goes instead.
                    if (shaped[i] === "-" && shaped.slice(i + 1) === typed.slice(i)) {
                        if (forwardDelete) {
                            typed = typed.slice(0, i) + typed.slice(i + 1);
                        } else if (i > 0) {
                            typed = typed.slice(0, i - 1) + typed.slice(i);
                            cursor = i - 1;
                        }
                    }
                }
                forwardDelete = false;
                const before = Math.min(25, typed.slice(0, cursor).replace(/[^0-9a-z]/gi, "").length);
                const next = typed.toUpperCase().replace(/[^0-9A-Z]/g, "").slice(0, 25).replace(/(.{5})(?=.)/g, "$1-");
                shaped = next;
                if (next !== text) {
                    text = next;
                }
                cursorPosition = before + Math.floor(Math.max(before - 1, 0) / 5);
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: WindowsMode.activating ? qsTr("Activating Windows. If Windows was asleep it starts first, which can take a few minutes.")
                                         : keyDialog.keyProblem
            size: 13
            color: WindowsMode.activating ? UI.Style.inkStatus : UI.Style.warning
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Accessible.role: Accessible.AlertMessage
        }

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                enabled: !WindowsMode.activating
                onClicked: keyDialog.close()
            },
            UI.Button {
                text: qsTr("Use this PC's licence")
                visible: page.licence.source !== "firmware"
                enabled: !WindowsMode.activating
                onClicked: {
                    keyDialog.keyProblem = "";
                    WindowsMode.setProductKey("firmware");
                }
            },
            UI.Button {
                kind: "primary"
                text: WindowsMode.activating ? qsTr("Activating") : qsTr("Use this key")
                enabled: !WindowsMode.activating && WindowsMode.keyShapeValid(keyField.text)
                onClicked: {
                    keyDialog.keyProblem = "";
                    WindowsMode.setProductKey(keyField.text);
                }
            }
        ]
    }

    // Activating with a Microsoft account (wildos-winmode, activation.py):
    // what happens before it starts, the steps while the Activate Windows
    // window is open, and how it went. The agent keeps the state, so closing
    // Settings part way and coming back shows the steps again.
    UI.Dialog {
        id: accountDialog
        size: "large"
        title: qsTr("Use a licence on my Microsoft account")
        closePolicy: phase === "intro" || phase === "result" ? T.Popup.CloseOnEscape : T.Popup.NoAutoClose

        // Set once Start is pressed, so the result shows instead of the
        // introduction when the activation ends.
        property bool started
        property string problem
        property string secret
        readonly property string phase: page.activation.state === "starting" ? "starting"
            : page.activation.state === "open" ? "open"
            : page.activation.state === "ending" ? "ending"
            : started && page.activation.result ? "result" : "intro"
        readonly property var outcome: page.activation.result || ({})

        text: {
            switch (phase) {
            case "starting":
                return qsTr("Starting Windows and adding the account for activating. This can take a minute or two, and Windows takes a moment more to sign in to a new account the first time.");
            case "open":
                return qsTr("The Activate Windows window is signed in to an account made just for this. In it:");
            case "ending":
                return qsTr("Signing out of the account for activating and removing it.");
            case "result":
                if (outcome.already) {
                    return qsTr("Windows is already activated, so there's nothing to move.");
                }
                if (outcome.activated) {
                    return qsTr("Windows is activated. The activation belongs to Windows mode's virtual PC, so it stays now the account for activating is removed.")
                        + (outcome.message ? " " + outcome.message : "");
                }
                return qsTr("Windows isn't activated.") + " "
                    + (outcome.message ? outcome.message : qsTr("The account for activating is removed."))
                    + " " + qsTr("You can try again, add a product key, or activate by phone: open the Windows desktop from Troubleshooting and run slui 4.");
            default:
                return qsTr("Windows mode adds a second Windows account just for this and opens the Windows desktop signed in to it, where you sign in to your Microsoft account and use Microsoft's activation troubleshooter. Afterwards Windows mode removes the account. Your own Windows account isn't changed, and Windows apps you have open leave the screen until it's done.");
            }
        }

        onOpened: {
            problem = "";
            started = page.activation.state !== "";
            secret = phase === "open" ? WindowsMode.activationPassword() : "";
        }
        onClosed: secret = ""
        onPhaseChanged: secret = phase === "open" ? WindowsMode.activationPassword() : ""

        Connections {
            target: WindowsMode
            function onAccountActivationDone(answer) {
                accountDialog.problem = answer;
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: accountDialog.phase === "intro"
            text: qsTr("The activation belongs to this virtual PC, so it stays after the account is removed. Microsoft only moves some licences: one that came from a free upgrade of a PC's original Windows often won't move to different hardware. If yours doesn't, a product key or activating by phone are the other ways.")
            size: 14
            color: UI.Style.inkStatus
            lineHeight: 1.55
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        Repeater {
            model: accountDialog.phase === "open" ? [
                qsTr("1. Open Settings, then Accounts, then Your info, and choose Sign in with a Microsoft account instead. When Windows asks for this account's current password, paste this one:"),
                qsTr("2. Then in Settings, open System, then Activation. Choose Troubleshoot, and when it has finished, I recently changed hardware on this device."),
                qsTr("3. Pick this device from the list and choose Activate. If Windows asks you to set up a PIN, it goes with the account.")
            ] : []

            ColumnLayout {
                required property string modelData
                required property int index
                Layout.fillWidth: true
                spacing: 8

                UI.Label {
                    Layout.fillWidth: true
                    text: modelData
                    size: 14
                    lineHeight: 1.4
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: index === 0 && accountDialog.secret.length > 0
                    spacing: 8

                    UI.TextField {
                        Layout.fillWidth: true
                        text: accountDialog.secret
                        readOnly: true
                        selectByMouse: true
                        font.family: "monospace"
                        Accessible.name: qsTr("Password of the account for activating")
                    }

                    UI.Button {
                        text: qsTr("Copy")
                        onClicked: WindowsMode.copySecret(accountDialog.secret)
                    }
                }
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: accountDialog.phase === "open"
            text: qsTr("Windows mode checks every few seconds and finishes by itself once Windows says it's activated. Done or Cancel removes the account straight away.")
            size: 13
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0 && accountDialog.phase !== "result"
            text: accountDialog.problem
            size: 13
            color: UI.Style.warning
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Accessible.role: Accessible.AlertMessage
        }

        buttons: [
            UI.Button {
                visible: accountDialog.phase === "intro"
                text: qsTr("Cancel")
                onClicked: accountDialog.close()
            },
            UI.Button {
                visible: accountDialog.phase === "intro"
                kind: "primary"
                text: qsTr("Open the Windows desktop")
                onClicked: {
                    accountDialog.problem = "";
                    accountDialog.started = true;
                    WindowsMode.activateWithAccount();
                }
            },
            UI.Button {
                visible: accountDialog.phase === "open"
                text: qsTr("Cancel")
                onClicked: WindowsMode.endAccountActivation()
            },
            UI.Button {
                visible: accountDialog.phase === "open"
                kind: "primary"
                text: qsTr("Done")
                onClicked: WindowsMode.endAccountActivation()
            },
            UI.Button {
                visible: accountDialog.phase === "starting" || accountDialog.phase === "ending"
                kind: "primary"
                text: accountDialog.phase === "starting" ? qsTr("Starting") : qsTr("Removing the account")
                busy: true
                enabled: false
            },
            UI.Button {
                visible: accountDialog.phase === "result"
                kind: "primary"
                text: qsTr("Close")
                onClicked: accountDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: foldersDialog
        title: qsTr("Folders Windows apps can see")
        text: qsTr("Windows apps can open and save files in these folders. A change applies the next time Windows starts.")

        property var chosen: []
        onOpened: chosen = (page.options.folders || []).slice()

        Repeater {
            model: [QT_TR_NOOP("Documents"), QT_TR_NOOP("Downloads"), QT_TR_NOOP("Pictures"), QT_TR_NOOP("Desktop"), QT_TR_NOOP("Music"), QT_TR_NOOP("Videos")]

            UI.CheckBox {
                required property string modelData
                text: qsTr(modelData)
                checked: foldersDialog.chosen.indexOf(modelData) >= 0
                onToggled: {
                    const list = foldersDialog.chosen.filter(f => f !== modelData);
                    if (checked) {
                        list.push(modelData);
                    }
                    foldersDialog.chosen = list;
                }
            }
        }

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: foldersDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Save")
                onClicked: {
                    const order = ["Documents", "Downloads", "Pictures", "Desktop", "Music", "Videos"];
                    page.show(WindowsMode.setOption("folders", order.filter(f => foldersDialog.chosen.indexOf(f) >= 0).join(",")));
                    foldersDialog.close();
                }
            }
        ]
    }

    // Read when it opens and dropped when it closes, so the password
    // isn't held in the page.
    UI.Dialog {
        id: passwordDialog
        title: qsTr("Windows password")
        text: secret.length > 0
            ? qsTr("The account is %1. Windows mode needs this password to sign in, so don't change it in Windows.").arg(account)
            : qsTr("The password isn't in your saved passwords, so there's nothing to show. Windows mode sets a new one the next time it starts.")

        readonly property string account: page.status.account || "wild"
        property string secret

        onOpened: secret = WindowsMode.windowsPassword(account)
        onClosed: secret = ""

        UI.TextField {
            visible: passwordDialog.secret.length > 0
            Layout.fillWidth: true
            text: passwordDialog.secret
            readOnly: true
            selectByMouse: true
            font.family: "monospace"
            Accessible.name: qsTr("Windows password")
        }

        buttons: [
            UI.Button {
                text: qsTr("Close")
                kind: passwordDialog.secret.length > 0 ? "secondary" : "primary"
                onClicked: passwordDialog.close()
            },
            UI.Button {
                visible: passwordDialog.secret.length > 0
                kind: "primary"
                text: qsTr("Copy")
                onClicked: {
                    WindowsMode.copySecret(passwordDialog.secret);
                    passwordDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: repairDialog
        title: qsTr("Repair Windows mode?")
        text: qsTr("Windows goes back to how it was when setup finished. Programs and settings changed in Windows since then are lost; your documents stay in your folders.")
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: repairDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Repair")
                onClicked: {
                    repairDialog.close();
                    page.show(WindowsMode.repair());
                }
            }
        ]
    }

    UI.Dialog {
        id: removeDialog
        title: qsTr("Remove Windows mode?")
        text: qsTr("Windows, its programs and their Start entries are deleted, which frees %1. Your documents stay in your folders.").arg(WindowsMode.size(page.status.diskBytes) || qsTr("the space they use"))
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: removeDialog.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Remove")
                onClicked: {
                    removeDialog.close();
                    page.show("");
                    WindowsMode.remove();
                }
            }
        ]
    }

    Connections {
        target: WindowsMode
        function onRemoveDone(answer) {
            page.show(answer);
        }
    }
}
