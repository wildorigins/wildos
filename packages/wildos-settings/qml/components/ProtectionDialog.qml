import QtQuick
import WildOS.Settings
import WildOS.UI as UI

// What one protection means on this computer and the one thing that can be
// done about it, opened from its tile on Privacy and security or its chip on
// Home. Where Wild OS cannot fix something itself, the text says how.
UI.Dialog {
    id: dialog

    property var item: null
    property string failure
    property bool firmwareRestart: false
    property bool startedGood: false

    readonly property var info: item ? Protection.details(item) : ({ text: "", action: "" })
    // 32-bit apps already turned off wait for the restart, which the switch
    // on Privacy and security offers.
    readonly property string action: (info.action === "firmware" && !firmwareRestart) || (info.action === "compat32" && !Compat32.enabled) ? "" : info.action

    function show(protection) {
        item = protection;
        failure = "";
        startedGood = Protection.good(protection);
        firmwareRestart = protection.key === "secureboot" && System.canRestartToFirmware();
        open();
    }

    title: item ? item.title : ""
    text: {
        if (failure.length > 0) {
            return failure;
        }
        if (info.action === "firmware" && !firmwareRestart) {
            return info.text + " " + qsTr("To get there, restart and press the key the computer names as it starts, often F2, F10, F12 or Delete.");
        }
        return info.text;
    }

    Connections {
        target: System
        function onUsbProtectionFailed(reason) {
            dialog.failure = reason;
        }
        function onFirmwareRestartFailed(reason) {
            dialog.failure = reason;
        }
        function onSecurityChanged() {
            // Fixed from here (USB protection turned on, updates found to be
            // current): the tile now says so, and the dialog has done its job.
            if (dialog.opened && dialog.item && !dialog.startedGood && Protection.good(dialog.item)) {
                dialog.close();
            }
        }
    }

    buttons: [
        UI.Button {
            visible: dialog.action.length > 0
            kind: "primary"
            busy: Protection.busy
            text: {
                switch (dialog.action) {
                case "updates": return qsTr("Install updates");
                case "check": return qsTr("Check for updates");
                case "usb": return qsTr("Turn on");
                case "firmware": return qsTr("Restart to firmware settings");
                case "restart": return qsTr("Restart now");
                case "compat32": return qsTr("Turn off");
                default: return "";
                }
            }
            onClicked: {
                switch (dialog.action) {
                case "updates":
                    dialog.close();
                    Navigator.open("update");
                    break;
                case "check":
                    Protection.checkNow();
                    break;
                case "usb":
                    System.setUsbProtection(true);
                    break;
                case "firmware":
                    System.restartToFirmware();
                    break;
                case "restart":
                    Updates.restart();
                    break;
                case "compat32":
                    // Takes effect at the next start-up; the switch on
                    // Privacy and security offers the restart.
                    dialog.close();
                    Compat32.setEnabled(false);
                    break;
                }
            }
        },
        UI.Button {
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    ]
}
