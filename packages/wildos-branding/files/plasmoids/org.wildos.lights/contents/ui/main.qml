pragma ComponentBehavior: Bound

import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasma5support as P5Support
import WildOS.UI as UI

// The camera and microphone lights (TaskbarCamMic.dc), left of the shield:
// amber while any app is using the microphone, green while one is using the
// camera, and the panel that says which app, since when, and cuts it off.
// Everything shown comes from /usr/libexec/wildos/camera-mic, which watches
// the devices themselves, so no app goes unseen by asking nobody. There is
// no setting that turns the light off.
PlasmoidItem {
    id: lights

    readonly property string libexec: "/usr/libexec/wildos/"

    // The amber and green of the board; deeper on a light taskbar so they
    // still read as lit.
    readonly property color amber: UI.Style.dark ? "#ffb454" : "#a35f00"
    readonly property color green: UI.Style.dark ? "#5fd38d" : "#1d7a4c"

    // What camera-mic last reported. Not "state": Item has one.
    property var report: ({})
    readonly property var microphone: device("microphone")
    readonly property var camera: device("camera")
    readonly property bool micInUse: microphone.uses.length > 0
    readonly property bool cameraInUse: camera.uses.length > 0
    readonly property bool inUse: micInUse || cameraInUse
    // Shown while switched off too: the panel is the only way to switch the
    // device back on.
    readonly property bool shown: inUse || microphone.blocked || camera.blocked
    property real now: Date.now()
    // Bumped when a switch's request ends, which puts the switch back on
    // the real state if nothing changed (a cancelled password prompt).
    property int requestCount: 0

    function device(name: string): var {
        const d = (report.devices || {})[name] || {};
        return { uses: d.uses || [], blocked: d.blocked === true, maximum: d.maximum === true, last: d.last || null };
    }

    function timeText(seconds: real): string {
        return Qt.locale().toString(new Date(seconds * 1000), UI.TimeFormat.shortFormat);
    }

    // Day and month names in the display language chosen in Settings, as the
    // clock's Formats.js picks them.
    function names(): var {
        const languages = Qt.locale().uiLanguages;
        const first = languages.length > 0 ? languages[0] : "";
        return first === "" || first === "C" || first.startsWith("en") ? Qt.locale("en_GB") : Qt.locale(first.replace("-", "_"));
    }

    // "today", "yesterday", a weekday within the week, then the date.
    function dayText(seconds: real): string {
        const when = new Date(seconds * 1000);
        const today = new Date(now);
        today.setHours(0, 0, 0, 0);
        const days = Math.round((today.getTime() - new Date(when.getFullYear(), when.getMonth(), when.getDate()).getTime()) / 86400000);
        if (days <= 0) {
            return qsTr("today");
        }
        if (days === 1) {
            return qsTr("yesterday");
        }
        if (days < 7) {
            return qsTr("on %1", "a weekday").arg(names().dayName(when.getDay(), Locale.LongFormat));
        }
        // A Qt date format, so a language can put its own word in the quotes.
        return names().toString(when, qsTr("'on' d MMMM", "a date format"));
    }

    function idleText(d: var): string {
        return d.last && d.last.app ? qsTr("Not in use. Last used %1 by %2.").arg(dayText(d.last.t)).arg(d.last.app) : qsTr("Not in use.");
    }

    readonly property string summary: {
        const parts = [];
        for (const u of microphone.uses) {
            parts.push(qsTr("Microphone in use by %1").arg(u.app));
        }
        for (const u of camera.uses) {
            parts.push(qsTr("Camera in use by %1").arg(u.app));
        }
        if (parts.length === 0) {
            if (microphone.blocked) {
                parts.push(qsTr("Microphone off for every app"));
            }
            if (camera.blocked) {
                parts.push(qsTr("Camera off for every app"));
            }
        }
        return parts.join(". ");
    }

    // camera-mic wait answers as soon as the state is newer than the
    // version it is given, so the light comes on as the app starts.
    P5Support.DataSource {
        id: watcher
        engine: "executable"
        property string version: "0"
        // Each wait is its own source (a numbered comment): the engine hands
        // a source it already has its last output.
        property int count: 0
        readonly property string command: lights.libexec + "camera-mic wait " + version + " #" + count
        onNewData: (source, data) => {
            disconnectSource(source);
            let next = {};
            try {
                next = JSON.parse(String(data.stdout));
            } catch (e) {
                next = {};
            }
            lights.report = next;
            lights.now = Date.now();
            const v = String(next.version || "0");
            // Without the service there is nothing to wait for; asking again
            // straight away would only spin.
            if (v === version || data["exit code"] !== 0) {
                retry.restart();
            } else {
                version = v;
                count += 1;
                Qt.callLater(() => watcher.connectSource(watcher.command));
            }
        }
    }

    Timer {
        id: retry
        interval: 5000
        onTriggered: {
            watcher.count += 1;
            watcher.connectSource(watcher.command);
        }
    }

    P5Support.DataSource {
        id: shell
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source);
            if (source.indexOf("camera-mic-block") >= 0) {
                lights.requestCount += 1;
            }
        }
    }

    property int runCount: 0

    function run(command: string): void {
        runCount += 1;
        shell.connectSource(command + " #" + runCount);
    }

    function stopUse(key: string): void {
        if (/^[0-9a-f]{16}$/.test(key)) {
            run(libexec + "camera-mic stop " + key);
        }
    }

    function setAllowed(name: string, on: bool): void {
        if (name === "microphone" || name === "camera") {
            run("pkexec " + libexec + "camera-mic-block " + (on ? "unblock " : "block ") + name);
        }
    }

    // Settings ships with Wild OS, but if it has gone the click still says
    // something rather than doing nothing.
    // A translated message can hold an apostrophe, which would end the
    // shell's single quotes.
    function shellQuote(text: string): string {
        return "'" + text.replace(/'/g, "'\\''") + "'";
    }

    function openHistory(): void {
        run("if command -v wildos-settings >/dev/null; then setsid -f wildos-settings --page history-camera >/dev/null 2>&1; "
            + "else notify-send -a 'Wild OS' -i preferences-system " + shellQuote(qsTr("Settings is missing")) + " "
            + shellQuote(qsTr("Reinstall it from the Store to see History.")) + "; fi");
        expanded = false;
    }

    onExpandedChanged: () => {
        if (lights.expanded) {
            now = Date.now();
        }
    }

    // Nothing to show, nothing to open.
    onShownChanged: () => {
        if (!lights.shown) {
            lights.expanded = false;
        }
    }

    Timer {
        running: lights.expanded
        interval: 30000
        repeat: true
        onTriggered: lights.now = Date.now()
    }

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: "camera-web"
    Plasmoid.title: qsTr("Camera and microphone")
    Plasmoid.status: shown ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.HiddenStatus
    toolTipMainText: summary
    toolTipSubText: qsTr("Click for details")

    preferredRepresentation: compactRepresentation
    compactRepresentation: Light {
        lights: lights
    }
    fullRepresentation: Panel {
        lights: lights
    }

    Component.onCompleted: {
        if (Plasmoid.hasOwnProperty("activationTogglesExpanded")) {
            Plasmoid.activationTogglesExpanded = true;
        }
        watcher.connectSource(watcher.command);
    }
}
