import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The licence card and the requirements of 9.1, which first run shows on
// 3.9 as well. Every row is a real check: the cores the processor has,
// CPUID and /dev/kvm, the memory fitted, the space where Windows mode keeps
// Windows, the connection, and the licence table in the firmware. A row
// says what is needed and what this PC has; one that isn't met turns amber
// and says what to do instead.
ColumnLayout {
    id: list

    // The Internet row's "Use a Windows 11 file you have": the setup window
    // offers it, first run doesn't.
    property bool isoLink: false
    signal isoRequested()

    readonly property bool ownIso: Setup.isoState === "ok"
    readonly property string editionName: Setup.edition === "pro" ? "Windows 11 Pro" : Setup.edition === "home" ? qsTr("Windows 11 Home") : ""

    Layout.fillWidth: true

    component Check: CheckRow {
        minHeight: 48
        verticalPadding: 7
    }

    Banner {
        readonly property bool home: Setup.licence !== "checking" && Setup.edition === "home"
        tone: Setup.licence === "checking" ? "accent" : Setup.firmwareUsable ? "success" : "warning"
        title: {
            if (home && (Setup.licence === "firmware" || Setup.licence === "inuse")) {
                return qsTr("This PC came with Windows 11 Home");
            }
            switch (Setup.licence) {
            case "checking": return qsTr("Looking for a Windows licence");
            case "firmware":
            case "inuse": return qsTr("Windows licence found");
            case "error": return qsTr("The licence check didn't finish");
            default: return qsTr("No Windows licence built into this PC");
            }
        }
        text: {
            if (home && (Setup.licence === "firmware" || Setup.licence === "inuse")) {
                return qsTr("Windows mode needs Windows 11 Pro, so this licence can't activate it. Enter a Pro key on the next step, or activate later.");
            }
            switch (Setup.licence) {
            case "checking": return qsTr("Checking this PC's firmware.");
            case "firmware":
                return list.editionName.length > 0 ? qsTr("%1 is built into this PC, so Windows mode uses it. There is nothing to buy.").arg(list.editionName)
                     : qsTr("A Windows licence is built into this PC, so Windows mode uses it. Windows confirms whether it's Home or Pro while it sets itself up.");
            // One licence covers one copy of Windows, and the one still on
            // this PC is a copy, so this isn't "nothing to buy".
            case "inuse":
                return list.editionName.length > 0
                     ? qsTr("%1 is built into this PC, so Windows mode can use it. Windows is still installed here too, and Microsoft's terms count each copy.").arg(list.editionName)
                     : qsTr("A Windows licence is built into this PC, so Windows mode can use it. Windows is still installed here too, and Microsoft's terms count each copy.");
            case "error": return qsTr("You can still enter a product key on the next step.");
            default: return qsTr("Enter a product key on the next step, or activate later.");
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            Check {
                title: qsTr("Virtualisation")
                readonly property bool nested: Setup.virtualisation === "ok" && Setup.nested
                state: Setup.checking && Setup.technology.length === 0 ? "checking" : Setup.virtualisation === "ok" && !nested ? "ok" : "warning"
                detail: {
                    if (nested) {
                        return qsTr("Wild OS is in a virtual machine, so Windows would run inside a second one and be very slow");
                    }
                    switch (Setup.virtualisation) {
                    case "ok": return qsTr("Switched on in this PC's firmware");
                    case "unsupported": return qsTr("This processor doesn't have it, so Windows mode can't run here");
                    case "noaccess": return qsTr("Only available at the PC's own screen");
                    default: return Setup.canRestartToFirmware ? qsTr("Switched off. Switch it on in this PC's firmware settings, then check again")
                                                               : qsTr("Switched off. Switch it on in this PC's firmware settings");
                    }
                }
                value: nested ? qsTr("Virtual machine") : Setup.virtualisation === "ok" ? Setup.technology : Setup.virtualisation === "off" ? qsTr("Off") : ""
            }

            Check {
                title: qsTr("Processor")
                state: Setup.cores === 0 && Setup.checking ? "checking" : Setup.coresOk ? "ok" : "warning"
                detail: Setup.coresOk || Setup.cores === 0 ? qsTr("64-bit with 4 cores or more needed")
                    : qsTr("4 cores or more needed. Windows mode would be too slow on this processor")
                value: Setup.cores === 1 ? qsTr("1 core") : Setup.cores > 0 ? qsTr("%1 cores").arg(Setup.cores) : ""
            }

            Check {
                title: qsTr("Memory")
                state: Setup.memoryLabel.length === 0 ? "checking" : Setup.memoryState === "ok" ? "ok" : "warning"
                detail: Setup.memoryState === "ok" ? qsTr("8 GB or more needed. Windows mode only borrows it while you use it")
                    : qsTr("8 GB or more needed. Fit more memory to use Windows mode")
                value: Setup.memoryLabel
            }

            Check {
                title: qsTr("Space")
                state: Setup.checking && Setup.freeLabel.length === 0 ? "checking" : Setup.spaceOk ? "ok" : "warning"
                detail: Setup.spaceOk ? qsTr("About %1 needed").arg(Setup.needLabel)
                    : qsTr("About %1 needed. Free some up in Settings, under Storage").arg(Setup.needLabel)
                value: Setup.freeLabel.length > 0 ? qsTr("%1 free").arg(Setup.freeLabel) : ""
            }

            Check {
                title: qsTr("Internet")
                state: Setup.internet === "checking" ? "checking" : Setup.internet === "offline" ? "warning" : "ok"
                // Short enough to sit on one line beside the link and the value.
                detail: list.ownIso ? (Setup.internet === "offline" ? qsTr("Connect to download the drivers Windows needs, about 1 GB")
                                                                    : qsTr("Only the drivers Windows needs download, about 1 GB"))
                      : Setup.internet === "offline" ? qsTr("Connect to the internet to download Windows, about 6 GB")
                      : Setup.internet === "metered" ? qsTr("On a metered connection. Windows is about 6 GB")
                      : qsTr("Windows downloads from Microsoft, about 6 GB")
                value: Setup.internet === "offline" ? qsTr("Not connected") : Setup.internet === "checking" ? "" : qsTr("Connected")
                action: !list.isoLink ? "" : Setup.isoState === "checking" ? qsTr("Checking the file") : list.ownIso ? qsTr("Download instead") : qsTr("Use a Windows 11 file you have")
                actionEnabled: Setup.isoState !== "checking"
                onActionClicked: list.ownIso ? Setup.forgetIso() : list.isoRequested()
            }
        }
    }
}
