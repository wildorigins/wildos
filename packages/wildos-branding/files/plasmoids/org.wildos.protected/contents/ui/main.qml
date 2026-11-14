pragma ComponentBehavior: Bound

import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasma5support as P5Support
import WildOS.UI as UI

// The "Protected" chip on the taskbar and its flyout, in the manner of Windows
// Security: a plain answer to "is my computer safe?", then the few things
// people care about, each with the one thing to do about it. Everything shown
// comes from /usr/libexec/wildos/security-status and usb-devices, so it never
// claims protection the machine does not have.
PlasmoidItem {
    id: security

    readonly property string libexec: "/usr/libexec/wildos/"
    // Articles on Wild OS's help site, help.wildos.io. A topic without an
    // article of its own opens the site's front page until it has one.
    readonly property string helpBase: "https://help.wildos.io/"
    readonly property var helpArticles: ({
        "encryption": "install/encryption-and-recovery-key/",
    })

    // key -> value, as printed by security-status.
    property var status: ({})
    readonly property bool checked: Object.keys(status).length > 0
    property real lastChecked: 0
    property real now: Date.now()

    // USB devices USBGuard is holding back: {port, kind, name}.
    property var blockedDevices: []
    // Keyed by port and name, so the same device plugged in again is new.
    property var dismissed: ({})
    property var notified: ({})
    // port -> true while allowing; port -> message when that failed.
    property var busy: ({})
    property var errors: ({})
    property string notifiedUpdatesSince: ""
    // A new warning has to be seen twice in a row before it is shown, so a
    // service a few seconds late at login never flashes "Action needed".
    property bool warningSeenOnce: false

    readonly property var pendingDevices: blockedDevices.filter(d => !dismissed[deviceKey(d)])
    readonly property var areas: buildAreas(status, pendingDevices.length)
    readonly property var warnings: areas.filter(a => a.tone === "warn")
    // Until the list of updates has been refreshed, nobody can say whether a
    // security update is waiting, so the computer is not called protected.
    // A new install has no list until its first check.
    readonly property bool updatesUnchecked: checked && status.updates === "unknown"
    readonly property bool updatesNeverChecked: updatesUnchecked && number(status.updates_checked) === 0
    // "checking", "unchecked", "protected" or "attention". Not "state": Item
    // has one.
    readonly property string level: !checked ? "checking" : warnings.length > 0 ? "attention"
        : updatesUnchecked ? "unchecked" : "protected"
    readonly property string chipText: level === "attention" ? "Action needed"
        : level === "checking" ? "Checking"
        : level === "unchecked" ? "Check updates" : "Protected"
    readonly property string uncheckedTitle: updatesNeverChecked ? "Updates not checked yet" : "Updates not checked lately"

    function number(value: var): int {
        const n = parseInt(value, 10);
        return isNaN(n) ? 0 : n;
    }

    function deviceKey(d: var): string {
        return d.port + "|" + d.name;
    }

    function plural(n: int, one: string, many: string): string {
        return n === 1 ? one : n + " " + many;
    }

    // Each area: the row's icon, title and status, how loud the status is,
    // the button label if there is something to do, and what a click does.
    function buildAreas(s: var, pending: int): var {
        const list = [];
        const row = (id, icon, title, text, tone, action, run) =>
            list.push({ id: id, icon: icon, title: title, status: text, tone: tone, action: action, run: run });

        // Only a row while it is off, switched off in Settings or failed to
        // load, and never one that can be ignored: the shield stays amber
        // until the firewall is back.
        if (s.firewall === "off") {
            row("firewall", "firewall", "Firewall", "Firewall is off", "warn", "Turn on", "settings:firewall");
        }

        if (s.encryption === "on") {
            row("encryption", "lock", "Disk encryption", "Your files are encrypted", "ok", "", "privacy");
        } else if (s.encryption === "off") {
            row("encryption", "lock", "Disk encryption", "This disk isn't encrypted", "warn", "Learn how", "help:encryption");
        } else {
            row("encryption", "lock", "Disk encryption", "You can turn this on when you install", "quiet", "", "help:encryption");
        }

        const securityUpdates = number(s.updates_security);
        if (s.updates === "waiting") {
            row("updates", "refresh", "Security updates",
                plural(securityUpdates, "1 security update is ready", "security updates are ready"),
                "warn", "Install now", "updates");
        } else if (s.updates === "current") {
            row("updates", "refresh", "Security updates",
                number(s.updates_other) > 0 ? "No security updates waiting" : "You're up to date",
                "ok", "", "updates");
        } else if (s.updates === "na") {
            row("updates", "refresh", "Security updates", "Updates start once Wild OS is installed", "quiet", "", "privacy");
        } else {
            const checkedAt = number(s.updates_checked);
            const days = checkedAt > 0 ? Math.floor((now / 1000 - checkedAt) / 86400) : 0;
            row("updates", "refresh", "Security updates",
                days >= 1 ? "Not checked for " + plural(days, "1 day", "days") : "Not checked yet",
                "quiet", "Check now", "updates");
        }

        if (s.apps === "off") {
            row("apps", "box", "App protection", "Apps aren't being contained", "warn", "Learn how", "help:app-protection");
        } else {
            row("apps", "box", "App protection", "Apps are limited in what they can reach", "ok", "", "privacy");
        }

        if (s.devices === "off") {
            row("devices", "plug", "Device protection", "New USB devices aren't checked", "warn", "Learn how", "help:usb-protection");
        } else if (pending > 0) {
            row("devices", "plug", "Device protection",
                plural(pending, "1 USB device is blocked", "USB devices are blocked"), "note", "Review", "page:usb");
        } else {
            row("devices", "plug", "Device protection", "New USB devices need your OK", "ok", "", "privacy");
        }

        if (s.network === "off") {
            row("network", "globe", "Network protection", "Other computers can reach some apps", "quiet", "", "privacy");
        } else {
            row("network", "globe", "Network protection", "Other computers can't connect to this one", "ok", "", "privacy");
        }

        // A choice made in Settings, so it is listed but never a warning.
        if (s.compat32 === "on") {
            row("compat32", "chip", "32-bit apps", "On, for Steam and older apps", "quiet", "", "privacy");
        }

        // Only worth a row when it could be on and is not; on a BIOS machine
        // there is nothing anyone can do about it.
        if (s.secureboot === "off") {
            row("secureboot", "chip", "Startup protection", "Secure Boot is off", "warn", "How to turn it on", "help:secure-boot");
        }

        // A warning the person chose to ignore stays listed but quiet, with
        // the way back; one they could ignore gets an Ignore button. Security
        // updates are never in either set.
        const ignored = String(s.ignored || "").split(",").filter(k => k.length > 0);
        const ignorable = ["encryption", "apps", "devices", "secureboot"];
        for (const area of list) {
            area.ignorable = false;
            if (area.tone !== "warn" || !ignorable.includes(area.id)) {
                continue;
            }
            if (ignored.includes(area.id)) {
                area.tone = "quiet";
                area.status += ". Ignored";
                area.action = "Show again";
                area.run = "unignore:" + area.id;
            } else {
                area.ignorable = true;
            }
        }
        return list;
    }

    function checkedText(): string {
        const minutes = Math.floor((now - lastChecked) / 60000);
        if (minutes < 1) {
            return "Checked just now";
        }
        if (minutes < 60) {
            return "Checked " + minutes + " min ago";
        }
        return "Checked at " + Qt.locale().toString(new Date(lastChecked), UI.TimeFormat.shortFormat);
    }

    P5Support.DataSource {
        id: checker
        engine: "executable"
        readonly property string command: security.libexec + "security-status"
        // Each refresh is its own source (a numbered comment), as with run():
        // the engine hands a source it already has its last output, so the
        // same command reconnected could report the state from before an
        // Ignore.
        property string current: command
        connectedSources: [current]
        // Most of this rarely changes, but a minute keeps the chip honest
        // after a service stops or updates arrive.
        interval: 60000
        onNewData: (source, data) => security.receiveStatus(data.stdout)
    }

    function receiveStatus(stdout: string): void {
        const next = {};
        for (const line of String(stdout).trim().split("\n")) {
            const at = line.indexOf("=");
            if (at > 0) {
                next[line.slice(0, at)] = line.slice(at + 1);
            }
        }
        if (Object.keys(next).length === 0) {
            return;
        }
        const before = checked ? buildAreas(status, 0).filter(a => a.tone === "warn").length : 0;
        const after = buildAreas(next, 0).filter(a => a.tone === "warn").length;
        if (after > before && !warningSeenOnce) {
            warningSeenOnce = true;
            recheck.restart();
            return;
        }
        warningSeenOnce = false;
        status = next;
        lastChecked = Date.now();
        now = lastChecked;
        maybeNotifyUpdates();
    }

    property int checkCount: 0

    function refreshStatus(): void {
        checker.disconnectSource(checker.current);
        checkCount += 1;
        checker.current = checker.command + " #" + checkCount;
        checker.connectSource(checker.current);
    }

    // The status is read again just after an Ignore or Show again, once the
    // command has written the list.
    Timer {
        id: ignoreRefresh
        interval: 400
        onTriggered: security.refreshStatus()
    }

    Timer {
        id: recheck
        interval: 10000
        onTriggered: security.refreshStatus()
    }

    // The list once at start and after every change; "watch" sits waiting for
    // udev to report a USB device, so a blocked one shows up within a second
    // or two without polling.
    P5Support.DataSource {
        id: usb
        engine: "executable"
        readonly property string listCommand: security.libexec + "usb-devices list"
        readonly property string watchCommand: security.libexec + "usb-devices watch"
        connectedSources: [listCommand, watchCommand]
        onNewData: (source, data) => {
            security.receiveDevices(data.stdout);
            disconnectSource(source);
            if (source === watchCommand) {
                Qt.callLater(() => usb.connectSource(watchCommand));
            }
        }
    }

    function refreshDevices(): void {
        usb.disconnectSource(usb.listCommand);
        usb.connectSource(usb.listCommand);
    }

    function validPort(port: string): bool {
        return /^[0-9]+-[0-9]+(\.[0-9]+)*$/.test(port);
    }

    function receiveDevices(stdout: string): void {
        const list = [];
        for (const line of String(stdout).split("\n")) {
            const parts = line.split("\t");
            if (parts.length >= 3 && validPort(parts[0])) {
                list.push({ port: parts[0], kind: parts[1], name: parts[2] });
            }
        }
        const present = {};
        const ports = {};
        const nextNotified = {};
        const nextDismissed = {};
        for (const d of list) {
            const key = deviceKey(d);
            present[key] = true;
            ports[d.port] = true;
            nextNotified[key] = true;
            if (dismissed[key]) {
                nextDismissed[key] = true;
            }
            if (!notified[key] && !dismissed[key]) {
                run(libexec + "security-notify usb " + d.port);
            }
        }
        // Forgetting unplugged devices means plugging one in again is asked
        // about again, as a fresh event.
        notified = nextNotified;
        dismissed = nextDismissed;
        busy = onlyPorts(busy, p => ports[p] === true);
        errors = onlyPorts(errors, p => ports[p] === true);
        blockedDevices = list;
    }

    function maybeNotifyUpdates(): void {
        const since = number(status.updates_since);
        if (status.updates !== "waiting" || since === 0 || String(since) === notifiedUpdatesSince) {
            return;
        }
        if (Date.now() / 1000 - since > 86400) {
            notifiedUpdatesSince = String(since);
            run(libexec + "security-notify updates " + number(status.updates_security));
        }
    }

    // One-off commands. Their answers (a notification button, the result of
    // allowing a device) come back here by the command that was run.
    P5Support.DataSource {
        id: shell
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source);
            security.commandFinished(source, String(data.stdout).trim());
        }
    }

    // The numbered comment keeps every run its own source: the engine will not
    // start a command again while one with the same text is still waiting,
    // such as a notification for a device that has since been plugged back in.
    property int runCount: 0

    function run(command: string): void {
        runCount += 1;
        shell.connectSource(command + " #" + runCount);
    }

    function commandFinished(command: string, answer: string): void {
        const match = command.match(/ ([0-9]+-[0-9.]+) #/);
        const port = match ? match[1] : "";
        if (command.indexOf("security-notify usb ") >= 0) {
            if (answer === "allow") {
                allowDevice(port);
            } else if (answer === "keep") {
                keepBlocked(port);
            }
        } else if (command.indexOf("security-notify updates ") >= 0) {
            if (answer === "install") {
                openUpdates();
            }
        } else if (command.indexOf("usb-devices allow ") >= 0) {
            busy = onlyPorts(busy, p => p !== port);
            if (answer === "denied") {
                setError(port, "Only an administrator can allow USB devices.");
            } else if (answer === "failed") {
                setError(port, "This device couldn't be allowed. Unplug it and try again.");
            }
            refreshDevices();
            refreshStatus();
        }
    }

    function onlyPorts(map: var, keep: var): var {
        const next = {};
        for (const p in map) {
            if (keep(p)) {
                next[p] = map[p];
            }
        }
        return next;
    }

    function setError(port: string, message: string): void {
        const next = Object.assign({}, errors);
        next[port] = message;
        errors = next;
    }

    function allowDevice(port: string): void {
        if (!validPort(port) || busy[port]) {
            return;
        }
        const nextBusy = Object.assign({}, busy);
        nextBusy[port] = true;
        busy = nextBusy;
        errors = onlyPorts(errors, p => p !== port);
        run(libexec + "usb-devices allow " + port);
    }

    function keepBlocked(port: string): void {
        const next = Object.assign({}, dismissed);
        for (const d of blockedDevices) {
            if (d.port === port) {
                next[deviceKey(d)] = true;
            }
        }
        dismissed = next;
    }

    // Settings ships with Wild OS, but if it has gone the click still says
    // something rather than doing nothing.
    function openSettings(page: string): void {
        run("if command -v wildos-settings >/dev/null; then setsid -f wildos-settings --page " + page + " >/dev/null 2>&1; "
            + "else notify-send -a 'Wild OS' -i preferences-system 'Settings is missing' "
            + "'Reinstall it from the Store to change this.'; fi");
        expanded = false;
    }

    function openPrivacy(): void {
        openSettings("privacy");
    }

    // Security updates are system updates, which Settings installs; the
    // Store only handles apps.
    function openUpdates(): void {
        openSettings("update");
    }

    function act(what: string): void {
        if (what === "privacy") {
            openPrivacy();
        } else if (what === "updates") {
            openUpdates();
        } else if (what === "settings:firewall") {
            openSettings("firewall");
        } else if (what.indexOf("ignore:") === 0 || what.indexOf("unignore:") === 0) {
            const at = what.indexOf(":");
            const key = what.slice(at + 1);
            if (["encryption", "apps", "devices", "secureboot"].includes(key)) {
                run(libexec + "security-status " + what.slice(0, at) + " " + key);
                ignoreRefresh.restart();
            }
        } else if (what.indexOf("help:") === 0) {
            Qt.openUrlExternally(helpBase + (helpArticles[what.slice(5)] || ""));
            expanded = false;
        }
    }

    onExpandedChanged: () => {
        if (security.expanded) {
            now = Date.now();
            refreshStatus();
            refreshDevices();
        }
    }

    Timer {
        running: security.expanded
        interval: 30000
        repeat: true
        onTriggered: security.now = Date.now()
    }

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: "security-high"
    Plasmoid.title: "Security"
    Plasmoid.status: level === "attention" ? PlasmaCore.Types.NeedsAttentionStatus : PlasmaCore.Types.PassiveStatus
    toolTipMainText: level === "attention" ? "Action needed" : level === "checking" ? "Checking your protection"
        : level === "unchecked" ? uncheckedTitle : "Your computer is protected"
    toolTipSubText: level === "attention" ? warnings.map(a => a.status).join("\n") : "Click for details"

    preferredRepresentation: compactRepresentation
    compactRepresentation: Chip {
        security: security
    }
    fullRepresentation: Flyout {
        security: security
    }

    Component.onCompleted: {
        if (Plasmoid.hasOwnProperty("activationTogglesExpanded")) {
            Plasmoid.activationTogglesExpanded = true;
        }
    }
}
