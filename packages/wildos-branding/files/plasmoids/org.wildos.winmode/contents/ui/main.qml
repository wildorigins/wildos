pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasma5support as P5Support
import org.kde.plasma.workspace.dbus as DBus

// Windows mode's status at the left end of the taskbar, where Windows 11
// puts its weather (boards 4.13 and 4.14): what Windows mode is doing in a
// word, the memory it holds on hover, and a panel with processor use, memory
// and the Windows apps open. Everything comes from "wildos-winmode status",
// so it only appears once this account has Windows mode set up, or while
// it sets up: then a ring fills on the mark, "Setting up · 46%", and the
// panel shows setup's steps (boards 9.11 and 9.12).
PlasmoidItem {
    id: winmode

    readonly property string statusCommand: "wildos-winmode status --if-set-up"

    // The agent's and the system service's status (winmode/agent.py and
    // winmode/system.py), as last read.
    property var agent: null
    property var system: null
    // False when Windows mode is set up but its services didn't reply.
    property bool answering: true
    property real now: Date.now()

    // {t: ms, cpu: QEMU's processor seconds}, the last minute and a bit.
    property var samples: []
    // Used when QEMU's processor count can't be read.
    property int hostCores: 0

    readonly property var setupRecord: system ? system.setup : null
    readonly property bool settingUp: answering && setupRecord !== null && setupRecord.running === true
    readonly property bool setUp: !answering || (system !== null && (system.vm || "") !== ""
        && !(setupRecord && setupRecord.running) && (!setupRecord || setupRecord.stage === "done"))
    readonly property bool shown: (setUp || settingUp) && Plasmoid.configuration.shown
    readonly property int setupPercent: settingUp ? Number(setupRecord.percent) || 0 : 0
    // Board 9.14: straight after setup the status says Ready, until the
    // first Windows app opens or an hour has gone by; then the states of
    // board 4.13 as usual.
    readonly property bool justReady: setUp && answering && setupRecord !== null && setupRecord.stage === "done"
        && Number(setupRecord.finished) > 0 && now / 1000 - Number(setupRecord.finished) < 3600
        && openApps.length === 0 && !(agent && agent.lastLaunch)

    readonly property var openApps: agent && Array.isArray(agent.openApps) ? agent.openApps : systemWindows(system)
    readonly property int appCount: {
        const ids = {};
        for (const w of openApps) {
            ids[w.id] = true;
        }
        return Object.keys(ids).length;
    }

    // One of: settingup, ready, running, paused, saved, off, starting,
    // updating, attention. Not "state": Item has one.
    readonly property string level: settingUp ? (setupRecord.failed ? "attention" : "settingup")
        : justReady && baseLevel !== "attention" ? "ready" : baseLevel
    // What Windows itself is doing; the panel shows this even while the
    // chip still says Ready.
    readonly property string baseLevel: levelFor(system, agent, answering, openApps.length)
    readonly property string stateText: ({
        settingup: (setupRecord && setupRecord.paused ? "Setup paused · " : "Setting up · ") + setupPercent + "%",
        ready: "Ready",
        running: appCount === 0 ? "Running" : appCount === 1 ? "Running, 1 app" : "Running, " + appCount + " apps",
        paused: "Paused",
        saved: "Saved to disk",
        off: "Not running",
        starting: "Starting",
        updating: "Updating",
        attention: "Needs attention"
    })[level]

    function pillFor(state: string): string {
        return pillWords[state] || "";
    }
    readonly property var pillWords: ({
        settingup: "Setting up", ready: "Ready",
        running: "Running", paused: "Paused", saved: "Saved", off: "Off",
        starting: "Starting", updating: "Updating", attention: "Needs attention"
    })

    readonly property var qemu: system && system.qemu ? system.qemu : null
    readonly property real memoryHeld: qemu ? Number(qemu.rss_bytes) || 0 : 0
    readonly property real memoryLimit: system ? Number(system.memory_bytes) || 0 : 0
    readonly property int processors: qemu && qemu.vcpus > 0 ? qemu.vcpus : Math.max(1, hostCores)
    readonly property bool canSave: system !== null && system.save_to_disk === true
    readonly property bool busyAction: system !== null && !!system.action || pendingAction !== ""
    // The button just pressed, until the service says what it is doing.
    property string pendingAction: ""

    function levelFor(sys: var, ag: var, ok: bool, windows: int): string {
        if (!ok || !sys) {
            return "attention";
        }
        if (sys.state === "Error" || sys.error) {
            return "attention";
        }
        const busy = Array.isArray(sys.busy) ? sys.busy : [];
        if (busy.includes("update") || busy.includes("maintenance")) {
            return "updating";
        }
        if (ag && ag.starting && windows === 0) {
            return "starting";
        }
        return ({ Running: "running", Paused: "paused", Saved: "saved" })[sys.state] || "off";
    }

    // Without the agent, the system service's list still says what is open,
    // by program name.
    function systemWindows(sys: var): var {
        if (!sys || !Array.isArray(sys.windows)) {
            return [];
        }
        return sys.windows.filter(w => w && !w.owner && w.app).map(w => ({
            id: w.app, name: w.app.charAt(0).toUpperCase() + w.app.slice(1),
            icon: "wildos-windows-app", title: w.title || ""
        }));
    }

    function bytesText(bytes: real): string {
        if (bytes < 1073741824) {
            return Math.max(1, Math.round(bytes / 1048576)) + " MB";
        }
        const gb = bytes / 1073741824;
        return (gb >= 10 ? Math.round(gb) : Math.round(gb * 10) / 10) + " GB";
    }

    function plural(n: int, one: string, many: string): string {
        return n === 1 ? "1 " + one : n + " " + many;
    }

    // QEMU's processor seconds over the time between the first and last
    // sample, as a share of every processor Windows has.
    function share(from: var, to: var): real {
        const seconds = (to.t - from.t) / 1000;
        if (seconds <= 0) {
            return 0;
        }
        return Math.max(0, Math.min(100, (to.cpu - from.cpu) / seconds / processors * 100));
    }

    readonly property int processorPercent: samples.length < 2 ? 0 : Math.round(share(samples[0], samples[samples.length - 1]))

    // Each step between samples as {x: 0 to 1 across the last minute, y: %}.
    readonly property var processorTrace: {
        const out = [];
        const start = now - 60000;
        for (let i = 1; i < samples.length; i++) {
            out.push({ x: Math.max(0, (samples[i].t - start) / 60000), y: share(samples[i - 1], samples[i]) });
        }
        return out;
    }

    function addSample(q: var): void {
        if (!q || typeof q.cpu_seconds !== "number") {
            samples = [];
            return;
        }
        const t = Date.now();
        const last = samples.length > 0 ? samples[samples.length - 1] : null;
        // A lower count means a new QEMU (Windows started again).
        const kept = last && q.cpu_seconds >= last.cpu && q.pid === last.pid ? samples.filter(s => s.t >= t - 63000) : [];
        kept.push({ t: t, cpu: q.cpu_seconds, pid: q.pid });
        samples = kept;
    }

    function editionText(): string {
        const id = String(system && system.edition || "").toLowerCase();
        const names = {
            professional: "Windows 11 Pro", professionalworkstation: "Windows 11 Pro for Workstations",
            core: "Windows 11 Home", coresinglelanguage: "Windows 11 Home", corecountryspecific: "Windows 11 Home",
            enterprise: "Windows 11 Enterprise", education: "Windows 11 Education"
        };
        const name = names[id] || "Windows 11";
        const activated = setupRecord && setupRecord.licence ? setupRecord.licence.activated : null;
        return activated === true ? name + ", activated" : activated === false ? name + ", not activated" : name;
    }

    // "12 minutes", "2 hours": how long ago a moment was, in whole units.
    function agoText(at: real): string {
        const minutes = Math.floor((now / 1000 - at) / 60);
        if (minutes < 60) {
            return plural(Math.max(1, minutes), "minute", "minutes");
        }
        const hours = Math.floor(minutes / 60);
        return hours < 24 ? plural(hours, "hour", "hours") : plural(Math.floor(hours / 24), "day", "days");
    }

    // "Paused for 12 minutes", "Shut down 2 hours ago": from when Windows
    // went into the state it is in (system.py note_since), or the state
    // alone when the service can't know.
    function sinceText(vm: string, lead: string, ago: bool): string {
        const since = system ? system.since : null;
        const at = since && since.state === vm && since.at > 0 ? since.at : 0;
        if (!at) {
            return lead;
        }
        if (now / 1000 - at < 60) {
            return lead + " just now";
        }
        return ago ? lead + " " + agoText(at) + " ago" : lead + " for " + agoText(at);
    }

    // What the service is doing for a button, said in place of the state.
    readonly property string actionText: ({
        pause: "Pausing", save: "Saving to disk", keep: "Saving to disk", resume: "Resuming", start: "Starting Windows",
        restart: "Restarting Windows", shutdown: "Shutting down Windows"
    })[system && system.action ? system.action : pendingAction] || ""

    function subtitle(): string {
        if (actionText !== "") {
            return actionText;
        }
        switch (baseLevel) {
        case "running":
            return editionText();
        case "paused":
            return sinceText("paused", "Paused", false);
        case "saved":
            // The pill says Saved; "Saved to disk 25 minutes ago" is too
            // long for the panel's width.
            return sinceText("saved", "Saved", true);
        case "off":
            return sinceText("shut off", "Shut down", true);
        case "starting":
            return "Getting Windows ready";
        case "updating":
            return "Installing updates";
        }
        return "Needs attention";
    }

    // "about 20 seconds", from how long the last start or restore took on
    // this PC; empty until one has been timed.
    function aboutText(seconds: real): string {
        if (!(seconds > 0)) {
            return "";
        }
        if (seconds < 1.5) {
            return "about a second";
        }
        if (seconds < 10) {
            return "about " + Math.round(seconds) + " seconds";
        }
        if (seconds < 55) {
            return "about " + Math.round(seconds / 5) * 5 + " seconds";
        }
        return seconds < 90 ? "about a minute" : "about " + Math.round(seconds / 60) + " minutes";
    }

    // Saved or off: what it costs now and to come back.
    function restingText(): string {
        const timings = system && system.timings ? system.timings : {};
        const about = aboutText(Number(baseLevel === "saved" ? timings.restore : timings.start));
        const lead = baseLevel === "saved" ? "Ready in " : "Starts in ";
        return "Uses no memory or processor. " + (about ? lead + about + ", or by itself when a Windows app opens."
            : (baseLevel === "saved" ? "Ready again" : "Starts") + " by itself when a Windows app opens.");
    }

    // What the panel says the idle policy will do (winmode/lifecycle.py).
    // Saving to disk is off in the service for now, and then it pauses.
    function idleText(): string {
        if (!system) {
            return "";
        }
        const toDisk = canSave && (system.when_idle === "save" || (system.when_idle === "automatic" && system.on_battery));
        const seconds = Number(system.idle_seconds) || 60;
        const wait = seconds === 60 ? "a minute" : seconds % 60 === 0 ? plural(seconds / 60, "minute", "minutes") : plural(seconds, "second", "seconds");
        if (openApps.length > 0) {
            return (toDisk ? "Saves to disk " : "Pauses ") + wait + " after you close the last one.";
        }
        if (typeof system.idle_in === "number") {
            const left = Math.max(1, Math.round(system.idle_in));
            return "No Windows apps are open. " + (toDisk ? "Saves to disk in " : "Pauses in ") + plural(left, "second", "seconds") + ".";
        }
        return "";
    }

    function attentionText(): string {
        if (settingUp) {
            return (setupRecord.problem && setupRecord.problem.text) || "Windows mode couldn't finish setting up. Open the full window to try again.";
        }
        if (!answering || !system) {
            return "Windows mode isn't answering. Restart the PC, then try again.";
        }
        if (system.state === "Error") {
            return "Windows stopped unexpectedly. Restart the PC, then open a Windows app to start it again.";
        }
        const last = system.last && system.last.action === "save" ? "save Windows to disk" : "pause Windows";
        return "Windows mode couldn't " + last + ", so it's still using memory. Restarting the PC puts it right.";
    }

    P5Support.DataSource {
        id: reader
        engine: "executable"
        // Each read is its own source (a numbered comment): the engine hands
        // a source it already has its last output instead of running it.
        property string current: ""
        onNewData: (source, data) => {
            disconnectSource(source);
            if (source === current) {
                current = "";
                winmode.receive(String(data.stdout));
            }
        }
    }

    property int readCount: 0
    property int misses: 0

    // With the agent up, both services are asked over D-Bus, as the
    // command-line tool does: starting that tool every ten seconds cost
    // about one percent of a processor core. The tool stays for PCs where
    // the agent isn't running, since asking it over D-Bus would start it.
    property bool asking: false

    function unanswered(): void {
        asking = false;
        receive(JSON.stringify({ agent: null, system: null, answering: false }));
    }

    function refreshOverDBus(): void {
        if (asking) {
            return;
        }
        asking = true;
        const agentReply = DBus.SessionBus.asyncCall({
            service: "org.wildos.WindowsMode1",
            path: "/org/wildos/WindowsMode1",
            iface: "org.wildos.WindowsMode1",
            member: "Status"
        });
        agentReply.finished.connect(() => {
            if (agentReply.isError) {
                unanswered();
                return;
            }
            const hostReply = DBus.SystemBus.asyncCall({
                service: "org.wildos.WindowsMode1",
                path: "/org/wildos/WindowsMode1",
                iface: "org.wildos.WindowsMode1.Host",
                member: "Status"
            });
            hostReply.finished.connect(() => {
                if (hostReply.isError) {
                    unanswered();
                    return;
                }
                let agentStatus, hostStatus;
                try {
                    agentStatus = JSON.parse(String(agentReply.value));
                    hostStatus = JSON.parse(String(hostReply.value));
                } catch (e) {
                    unanswered();
                    return;
                }
                asking = false;
                receive(JSON.stringify({ agent: agentStatus, system: hostStatus }));
            });
        });
    }

    function refresh(): void {
        if (agentWatcher.registered) {
            refreshOverDBus();
            return;
        }
        if (reader.current !== "") {
            return;
        }
        readCount += 1;
        reader.current = statusCommand + " #" + readCount;
        reader.connectSource(reader.current);
    }

    function receive(stdout: string): void {
        let parsed;
        try {
            parsed = JSON.parse(stdout);
        } catch (e) {
            return;
        }
        if (!parsed || typeof parsed !== "object") {
            return;
        }
        // The services can be slow to answer while the desktop is still
        // starting, so one miss is never shown as a problem.
        if (parsed.answering === false && misses < 1) {
            misses += 1;
            return;
        }
        misses = 0;
        now = Date.now();
        answering = parsed.answering !== false;
        agent = parsed.agent && typeof parsed.agent === "object" ? parsed.agent : null;
        system = parsed.system && typeof parsed.system === "object" ? parsed.system : null;
        addSample(qemu);
        if (!shown) {
            expanded = false;
        }
    }

    // Every second while the panel is open, for the processor graph and the
    // memory figure; every ten seconds behind the chip; once a minute on a PC that hasn't
    // set Windows mode up, which costs a check for a file and nothing more,
    // unless the agent is up (it is whenever anything uses Windows mode).
    Timer {
        running: true
        repeat: true
        triggeredOnStart: true
        interval: winmode.expanded ? 1000 : winmode.setUp || winmode.settingUp || agentWatcher.registered ? 10000 : 60000
        onTriggered: winmode.refresh()
    }

    // The agent starts when setup does (from first run, or the setup
    // window), so the item shows at once instead of at the next minute.
    DBus.DBusServiceWatcher {
        id: agentWatcher
        busType: DBus.BusType.Session
        watchedService: "org.wildos.WindowsMode1"
        onRegisteredChanged: winmode.refresh()
    }

    // "Show progress" in the setup notification and Start's "Setting up"
    // button ask the agent, which counts the requests; each new one opens
    // the panel. Asked over D-Bus, not by starting a program, and only
    // while setup runs.
    property int progressRequests: -1

    function checkProgressRequests(): void {
        const reply = DBus.SessionBus.asyncCall({
            service: "org.wildos.WindowsMode1",
            path: "/org/wildos/WindowsMode1",
            iface: "org.wildos.WindowsMode1",
            member: "Status"
        });
        reply.finished.connect(() => {
            if (reply.isError) {
                return;
            }
            let count = -1;
            try {
                count = Number(JSON.parse(String(reply.value)).progressRequests);
            } catch (e) {
                return;
            }
            if (progressRequests >= 0 && count > progressRequests) {
                refresh();
                expanded = true;
            }
            progressRequests = count;
        });
    }

    Timer {
        running: winmode.settingUp && agentWatcher.registered
        repeat: true
        triggeredOnStart: true
        interval: 1000
        onTriggered: winmode.checkProgressRequests()
    }

    P5Support.DataSource {
        id: shell
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source);
            winmode.commandFinished(source, String(data.stdout).trim());
        }
    }

    property int runCount: 0

    function run(command: string): void {
        runCount += 1;
        shell.connectSource(command + " #" + runCount);
    }

    function commandFinished(command: string, answer: string): void {
        if (command.indexOf("nproc") === 0) {
            hostCores = parseInt(answer, 10) || 0;
            return;
        }
        if (command.indexOf("wildos-winmode power ") === 0) {
            pendingAction = "";
        }
        refresh();
    }

    // Pause, Save to disk, Resume, Start, Restart and Shut down, through the
    // agent, which asks first when Restart or Shut down could lose unsaved
    // work, and says so in a notification when something fails. The panel
    // closes for those two, so the question isn't hidden behind it.
    function power(action: string): void {
        if (["pause", "save", "resume", "start", "restart", "shutdown"].indexOf(action) < 0) {
            return;
        }
        pendingAction = action;
        run("wildos-winmode power " + action);
        if (action === "restart" || action === "shutdown") {
            expanded = false;
        }
    }

    // Pause and Resume of setup (board 9.12), and its full window.
    function setupControl(action: string): void {
        if (action === "window") {
            run("setsid -f wildos-winmode >/dev/null 2>&1");
            expanded = false;
            return;
        }
        DBus.SessionBus.asyncCall({
            service: "org.wildos.WindowsMode1",
            path: "/org/wildos/WindowsMode1",
            iface: "org.wildos.WindowsMode1",
            member: action === "pause" ? "PauseSetup" : "ResumeSetup"
        }).finished.connect(() => refresh());
    }

    // Settings ships with Wild OS, but if it has gone the click still says
    // something rather than doing nothing.
    function openSettings(): void {
        run("if command -v wildos-settings >/dev/null; then setsid -f wildos-settings --page windowsmode >/dev/null 2>&1; "
            + "else notify-send -a 'Wild OS' -i preferences-system 'Settings is missing' "
            + "'Reinstall it from the Store to change this.'; fi");
        expanded = false;
    }

    onExpandedChanged: () => {
        if (winmode.expanded) {
            now = Date.now();
            refresh();
        }
    }

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: "wildos-windows-app"
    Plasmoid.title: "Windows mode"
    Plasmoid.status: !shown ? PlasmaCore.Types.HiddenStatus
        : level === "attention" ? PlasmaCore.Types.NeedsAttentionStatus : PlasmaCore.Types.PassiveStatus
    toolTipMainText: "Windows mode: " + stateText.charAt(0).toLowerCase() + stateText.slice(1)
    toolTipSubText: level === "attention" ? attentionText()
        : memoryHeld > 0 ? bytesText(memoryHeld) + " of memory held" : "Holding no memory"

    preferredRepresentation: compactRepresentation
    compactRepresentation: Chip {
        winmode: winmode
    }
    fullRepresentation: Loader {
        Layout.minimumWidth: item ? item.Layout.minimumWidth : 0
        Layout.preferredWidth: item ? item.Layout.preferredWidth : 0
        Layout.maximumWidth: item ? item.Layout.maximumWidth : 0
        Layout.minimumHeight: item ? item.Layout.minimumHeight : 0
        Layout.preferredHeight: item ? item.Layout.preferredHeight : 0
        Layout.maximumHeight: item ? item.Layout.maximumHeight : 0
        focus: true
        sourceComponent: winmode.settingUp ? setupPanel : statusPanel

        Component {
            id: statusPanel
            StatusPanel {
                winmode: winmode
            }
        }

        Component {
            id: setupPanel
            SetupPanel {
                winmode: winmode
            }
        }
    }

    Component.onCompleted: {
        if (Plasmoid.hasOwnProperty("activationTogglesExpanded")) {
            Plasmoid.activationTogglesExpanded = true;
        }
        run("nproc");
    }
}
