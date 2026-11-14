pragma Singleton

import QtQuick
import WildOS.Settings

// Reads what /usr/libexec/wildos/security-status reports and words it for
// people. Like the taskbar chip, the page never claims protection the
// machine does not have: anything the script cannot vouch for is shown as
// needing a look, not as fine.
QtObject {
    readonly property var status: System.security

    // The firewall's switch changes what the tiles say at once, not at the
    // next check.
    property Connections firewallSwitch: Connections {
        target: Firewall
        function onDone(what) {
            if (what === "enabled") {
                System.refreshSecurity();
            }
        }
    }

    // USB protection is Advanced protection's usb_locked; a failed or
    // cancelled change refreshes too, so the switches go back to the truth.
    property Connections advancedSwitch: Connections {
        target: Advanced
        function onBusyChanged() {
            if (!Advanced.busy) {
                System.refreshSecurity();
            }
        }
    }

    readonly property bool live: status.live === "yes"

    function count(key) {
        const n = parseInt(status[key] || "0");
        return isNaN(n) ? 0 : n;
    }

    // Keys the script prints for people, in the order the page shows them.
    // time and kernel are diagnostics and stay off the page.
    readonly property var known: [
        { key: "firewall", title: "Firewall", icon: "firewall", sentence: "the firewall" },
        { key: "encryption", title: "Disk encryption", icon: "lock", sentence: "disk encryption" },
        { key: "updates", title: "Security updates", icon: "update", sentence: "security updates" },
        { key: "apps", title: "App protection", icon: "sandbox", sentence: "app protection" },
        { key: "devices", title: "USB protection", icon: "plug", sentence: "USB protection" },
        { key: "secureboot", title: "Secure Boot", icon: "chip", sentence: "Secure Boot" },
        { key: "network", title: "Network protection", icon: "firewall", sentence: "network protection" },
        { key: "compat32", title: "32-bit apps", icon: "chip", sentence: "32-bit apps" }
    ]

    // 32-bit apps get a row only while they are on: off is how Wild OS
    // ships, and not a protection anyone needs to see listed.
    readonly property bool compat32: status.compat32 === "on"
    readonly property var items: known.filter(item => status[item.key] !== undefined && (item.key !== "compat32" || compat32))
    readonly property bool checked: items.length > 0

    // Warnings the person chose to ignore from the taskbar's security chip
    // (security-status ignore): shown as they are, but not raised.
    readonly property var ignored: String(status.ignored || "").split(",").filter(k => k.length > 0)

    // True when the item is fine, simply does not apply here, or is a
    // warning the person chose to ignore.
    function good(item) {
        if (ignored.includes(item.key)) {
            return true;
        }
        const v = status[item.key];
        switch (item.key) {
        case "updates":
            return v === "current" || v === "na";
        case "secureboot":
            return v === "on" || v === "unsupported";
        case "encryption":
            return v === "on" || v === "na";
        case "network":
            // Something listening can be what the person chose (sharing a
            // folder, say), so it is shown but never raised as a problem;
            // the taskbar's security chip words it the same way.
            return true;
        case "compat32":
            // A choice made in Settings with the trade-off spelled out, so
            // it is shown (and lowers the headline) but never raised.
            return true;
        default:
            return v === "on";
        }
    }

    readonly property var attention: items.filter(item => !good(item))

    // The page leads with the three tiles the design shows; any other
    // protection joins them only while it needs a look.
    readonly property var leading: ["firewall", "encryption", "updates"]
    readonly property var tiles: leading.map(key => items.find(item => item.key === key)).filter(item => item !== undefined)
        .concat(attention.filter(item => !leading.includes(item.key)))
        .concat(items.filter(item => item.key === "compat32"))
    readonly property bool allOn: checked && attention.length === 0

    function value(item) {
        const base = rawValue(item);
        return ignored.includes(item.key) && rawValue(item) !== "On" ? base + ", warning ignored" : base;
    }

    function rawValue(item) {
        const v = status[item.key];
        switch (item.key) {
        case "encryption":
            return v === "on" ? "On" : v === "na" ? "Not used in the live session" : "Off";
        case "updates":
            if (v === "current") {
                return "Up to date";
            }
            if (v === "waiting") {
                const n = count("updates_security");
                return n > 0 ? Format.plural(n, "update", "updates") + " waiting" : "Updates waiting";
            }
            if (v === "unknown") {
                return updatesNeverChecked ? "Not checked yet" : "Not checked for over a week";
            }
            return "Not used in the live session";
        case "devices":
            if (v !== "on") {
                return "Off";
            }
            return count("devices_blocked") > 0 ? "On, " + Format.plural(count("devices_blocked"), "device", "devices") + " held back" : "On";
        case "secureboot":
            return v === "on" ? "On" : v === "unsupported" ? "Not supported by this computer" : "Off";
        case "compat32":
            return "On, for Steam and older apps";
        case "network":
            if (v === "on") {
                return "On";
            }
            return count("network_services") > 0 ? Format.plural(count("network_services"), "service", "services") + " open to the network" : "Off";
        default:
            return v === "on" ? "On" : "Off";
        }
    }

    function problem(item) {
        const v = status[item.key];
        switch (item.key) {
        case "updates":
            return v === "unknown" ? (updatesNeverChecked ? "updates have not been checked yet" : "updates have not been checked for over a week")
                : Format.plural(Math.max(1, count("updates_security")), "security update is", "security updates are") + " waiting";
        case "network":
            return count("network_services") > 0
                ? Format.plural(count("network_services"), "service accepts", "services accept") + " connections from other computers"
                : "network protection is off";
        default:
            return item.sentence + " is off";
        }
    }

    readonly property bool updatesWaiting: status.updates === "waiting"
    // A new install has no list of updates until its first check, which is
    // not the same as a list gone a week without a refresh.
    readonly property bool updatesNeverChecked: status.updates === "unknown" && count("updates_checked") === 0

    // "Check now" on Privacy and Home. The status tool only reads what is on
    // the machine, so a real check first refreshes the list of updates (the
    // one thing that goes stale), then reads every protection again. It stays
    // on screen for at least a moment so a fast check is still seen to run.
    property bool checking: false
    property bool waitingForUpdates: false
    property string checkNote: ""
    readonly property bool busy: checking || System.securityChecking
    readonly property string checkStep: waitingForUpdates ? "Looking for security updates" : "Reading each protection"

    function checkNow() {
        if (busy) {
            return;
        }
        checking = true;
        checkNote = "";
        minimumCheck.restart();
        System.refreshAppPermissions();
        if (!live && Updates.available) {
            waitingForUpdates = true;
            Updates.check();
            if (Updates.state !== "checking") {
                waitingForUpdates = false;
                System.refreshSecurity();
            }
        } else {
            System.refreshSecurity();
        }
    }

    function finishCheck() {
        if (checking && !minimumCheck.running && !waitingForUpdates && !System.securityChecking) {
            checking = false;
        }
    }

    property Timer minimumCheck: Timer {
        interval: 1500
        onTriggered: finishCheck()
    }

    property Connections updatesWatch: Connections {
        target: Updates
        function onStateChanged() {
            if (waitingForUpdates && Updates.state !== "checking") {
                waitingForUpdates = false;
                if (Updates.state === "error") {
                    checkNote = Updates.error;
                }
                System.refreshSecurity();
            }
        }
    }

    property Connections securityWatch: Connections {
        target: System
        function onSecurityCheckingChanged() {
            finishCheck();
        }
    }

    // What each protection means and what can be done about it, for the
    // dialog a tile opens. action is one of "updates", "check", "usb",
    // "firmware", "restart", "compat32" or empty when there is nothing to
    // press.
    function details(item) {
        const v = status[item.key];
        switch (item.key) {
        case "encryption":
            if (v === "on") {
                return { text: "The disk is encrypted. Without your disk password, nobody can read your files, even with the drive taken out of this computer.", action: "" };
            }
            if (v === "na") {
                return { text: "You are running Wild OS from the install media, so nothing is stored on this computer yet. Encryption is offered when you install.", action: "" };
            }
            return { text: "Your files are not encrypted, so anyone with this computer's drive could read them. Encryption can only be set up while Wild OS is installed: to turn it on, back up your files and install Wild OS again with \"Encrypt Wild OS\" ticked.", action: "" };
        case "updates":
            if (v === "current") {
                return { text: "Every security update available for this computer is installed.", action: "" };
            }
            if (v === "waiting") {
                return { text: Format.plural(Math.max(1, count("updates_security")), "security update is", "security updates are") + " ready to install. They fix problems that could let someone into this computer.", action: "updates" };
            }
            if (v === "unknown") {
                return { text: updatesNeverChecked
                    ? "Wild OS has not checked for updates yet, so it cannot tell whether any are waiting."
                    : "Wild OS has not been able to check for updates for over a week, so it cannot tell whether any are waiting.", action: "check" };
            }
            return { text: "Updates are installed once Wild OS is on this computer.", action: "" };
        case "apps":
            if (v === "on") {
                return { text: "Apps are kept to the files and devices they need, so a misbehaving app cannot reach the rest of the system.", action: "" };
            }
            return { text: "App protection keeps each app to what it needs. It starts with the computer and is off this time, which happens when the computer was started with it switched off. Restart to turn it back on; if it is still off afterwards, it has been disabled in this computer's start-up settings.", action: "restart" };
        case "devices":
            if (v === "on") {
                return { text: count("devices_blocked") > 0
                    ? Format.plural(count("devices_blocked"), "device is", "devices are") + " held back. Allow the ones you trust from the shield on the taskbar."
                    : "Memory sticks, drives, a keyboard, a mouse, webcams and audio devices work straight away. Other new USB devices stay switched off until you allow them from the shield on the taskbar.", action: "" };
            }
            return { text: "Anything plugged into a USB port works straight away, including devices made to attack a computer. Turn this on to hold back new devices that are not ordinary drives, keyboards, mice, webcams or audio devices until you allow them.", action: "usb" };
        case "secureboot":
            if (v === "on") {
                return { text: "The computer checks that nothing has tampered with Wild OS before it starts.", action: "" };
            }
            if (v === "unsupported") {
                return { text: "This computer's firmware does not offer Secure Boot, so there is nothing to turn on.", action: "" };
            }
            return { text: "Secure Boot checks that nothing has tampered with Wild OS before it starts. It is switched on in the computer's firmware settings, not in Wild OS.", action: "firmware" };
        case "firewall":
            if (v === "on") {
                return { text: "The firewall turns away anything another computer sends that this one did not ask for. Everything you start can still reach the internet.", action: "" };
            }
            return { text: "The firewall did not load when this computer started, so other computers can reach anything that listens on the network. Restart to load it again; choosing a protection level in Privacy and security also loads it.", action: "restart" };
        case "network":
            if (v === "on") {
                return { text: "Nothing on this computer accepts connections from other computers.", action: "" };
            }
            return { text: Format.plural(Math.max(1, count("network_services")), "service accepts", "services accept") + " connections from other computers. Wild OS does not open any by itself, so an app you installed or a sharing feature you turned on started it. Remove or turn off the app that needs it if you do not use it.", action: "" };
        case "compat32":
            return { text: "Steam and 32-bit apps is on, so 32-bit programs such as Steam and older games can run. To run them, the system keeps a part that Wild OS normally leaves out, and that part has had serious security flaws over the years. Apps you run can also create a virtual keyboard or game controller, which Steam needs for controllers. Turn it off when you no longer need it. It takes a restart.", action: "compat32" };
        default:
            return { text: "", action: "" };
        }
    }

    function list(parts) {
        if (parts.length <= 1) {
            return parts.join("");
        }
        return parts.slice(0, -1).join(", ") + " and " + parts[parts.length - 1];
    }

    function sentenceCase(text) {
        return text.length > 0 ? text.charAt(0).toUpperCase() + text.slice(1) : text;
    }

    readonly property string headline: !checked ? "Wild OS could not check your protection"
        : allOn && compat32 ? "Protected, with 32-bit apps on"
        : allOn ? "Your computer is protected"
        : attention.length === 1 && attention[0].key === "updates" && updatesWaiting ? "Security updates are waiting"
        : attention.length === 1 && attention[0].key === "updates" ? (updatesNeverChecked ? "Updates not checked yet" : "Updates not checked lately")
        : "Some protection needs a look"

    function description(checkedAt) {
        const when = Format.ago(checkedAt);
        const suffix = when.length > 0 ? " Last checked " + when + "." : "";
        if (!checked) {
            return "The protection check is not installed on this computer.";
        }
        if (allOn && compat32) {
            return "32-bit apps are on, which keeps a part of the system that has had serious security flaws. Everything else Wild OS checks is in order." + suffix;
        }
        if (allOn) {
            const onNames = tiles.filter(item => status[item.key] === "on" || status[item.key] === "current")
                .slice(0, 3).map(item => item.sentence);
            return onNames.length > 1
                ? sentenceCase(list(onNames)) + " are all on." + suffix
                : "Everything Wild OS checks is in order." + suffix;
        }
        return sentenceCase(list(attention.map(problem))) + "." + suffix;
    }
}
