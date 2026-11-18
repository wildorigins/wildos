pragma ComponentBehavior: Bound

import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasma5support as P5Support
import WildOS.UI as UI

// The tray's quick settings: one button showing network, sound and battery,
// opening the design's quick settings flyout. Every backend is loaded on its
// own, so a machine missing one of them (no Bluetooth stack, no battery
// service) still gets the rest, with the missing parts switched off calmly.
PlasmoidItem {
    id: quick

    readonly property var network: networkLoader.item
    readonly property var audio: audioLoader.item
    readonly property var bluetooth: bluetoothLoader.item
    readonly property var power: powerLoader.item
    readonly property var dnd: dndLoader.item
    readonly property var access: accessLoader.item

    Loader { id: networkLoader; source: "NetworkBackend.qml" }
    Loader { id: audioLoader; source: "AudioBackend.qml" }
    Loader { id: bluetoothLoader; source: "BluetoothBackend.qml" }
    Loader { id: powerLoader; source: "PowerBackend.qml" }
    Loader { id: dndLoader; source: "NotificationsBackend.qml" }
    Loader { id: accessLoader; source: "AccessibilityBackend.qml" }

    // What the tray shows for the network: "flight", "wired", "offline", or
    // "wifi" with a signal level. Read from plasma-nm's own icon choice, so it
    // always agrees with NetworkManager.
    readonly property string networkKind: {
        if (!network) {
            return "offline";
        }
        const name = network.iconName;
        if (network.flightMode || name.indexOf("flightmode") >= 0) {
            return "flight";
        }
        if (network.wiredConnected || (name.indexOf("wired") >= 0 && name.indexOf("disconnected") < 0)) {
            return "wired";
        }
        if (/(wireless|mobile)-\d+/.test(name) || network.activeWifiName.length > 0) {
            return "wifi";
        }
        if (network.vpnActive) {
            return network.wifiAvailable ? "wifi" : "wired";
        }
        return "offline";
    }

    readonly property int wifiBars: {
        if (!network) {
            return -1;
        }
        const match = /-(\d+)/.exec(network.iconName);
        if (!match) {
            return networkKind === "wifi" ? 3 : -1;
        }
        const level = Number(match[1]);
        return level >= 70 ? 3 : level >= 45 ? 2 : level >= 20 ? 1 : 0;
    }

    readonly property bool connected: network !== null && (network.wiredConnected || network.wifiConnected)

    // How the connection is doing, in the words Windows uses.
    readonly property string connectionWords: {
        if (!network) {
            return qsTr("Not connected");
        }
        if (network.connecting && !connected) {
            return qsTr("Connecting");
        }
        if (!connected) {
            return qsTr("Not connected");
        }
        if (network.portal) {
            return qsTr("Sign-in needed");
        }
        if (network.internetKnown && !network.internet) {
            return qsTr("No internet");
        }
        return qsTr("Connected");
    }

    readonly property string networkTooltip: {
        if (network && network.flightMode && !connected) {
            return qsTr("Network: Flight mode is on");
        }
        if (!connected) {
            return qsTr("Network: %1").arg(connectionWords);
        }
        const where = network.wifiConnected ? network.activeWifiName : qsTr("Connected");
        if (network.portal) {
            return qsTr("Network: %1, sign-in needed").arg(where);
        }
        if (network.internetKnown) {
            return network.internet ? qsTr("Network: %1, internet access").arg(where) : qsTr("Network: %1, no internet").arg(where);
        }
        return qsTr("Network: %1").arg(where);
    }

    function duration(msec: real): string {
        const minutes = Math.round(msec / 60000);
        if (minutes < 60) {
            const shown = minutes < 10 ? Math.max(1, minutes) : Math.round(minutes / 5) * 5;
            return shown === 1 ? qsTr("1 minute") : qsTr("%1 minutes").arg(shown);
        }
        const hours = Math.round(minutes / 60);
        return hours === 1 ? qsTr("1 hour") : qsTr("%1 hours").arg(hours);
    }

    readonly property string batteryDetail: {
        if (!power || !power.hasBattery) {
            return "";
        }
        if (power.fullyCharged) {
            return qsTr("Fully charged");
        }
        if (power.notCharging) {
            return qsTr("Plugged in, not charging");
        }
        if (power.charging) {
            return power.remainingMsec > 0 ? qsTr("About %1 until full").arg(duration(power.remainingMsec)) : qsTr("Charging");
        }
        return power.remainingMsec > 0 ? qsTr("About %1 left").arg(duration(power.remainingMsec)) : "";
    }

    readonly property bool batteryLow: power !== null && power.hasBattery && !power.pluggedIn && power.percent <= 10

    // The design's "Edit quick settings": the tiles someone has taken away.
    readonly property var hiddenTiles: Plasmoid.configuration.hiddenTiles
    readonly property var addedTiles: Plasmoid.configuration.addedTiles

    // The tiles in the order someone dragged them into, if they have.
    readonly property var tileOrder: Plasmoid.configuration.tileOrder

    function setTileOrder(order: var): void {
        Plasmoid.configuration.tileOrder = order;
    }

    function setTileShown(id: string, shown: bool): void {
        const hidden = hiddenTiles.filter(t => t !== id);
        const added = addedTiles.filter(t => t !== id);
        if (shown) {
            added.push(id);
        } else {
            hidden.push(id);
        }
        Plasmoid.configuration.hiddenTiles = hidden;
        Plasmoid.configuration.addedTiles = added;
    }

    // Short-lived shell commands: night light settings and starting Settings.
    P5Support.DataSource {
        id: shell
        engine: "executable"
        onNewData: source => disconnectSource(source)
    }

    function run(command: string): void {
        shell.connectSource(command);
    }

    // Keep awake: until a set time, or until turned off when keepAwakeUntil
    // is 0. The block itself lives in powerdevil, so if something else
    // releases it the timer here is dropped too.
    readonly property bool keepingAwake: power !== null && power.keepingAwake
    property real keepAwakeUntil: 0

    function keepAwake(minutes: int): void {
        if (!power) {
            return;
        }
        if (minutes < 0) {
            keepAwakeUntil = 0;
            keepAwakeTimer.stop();
            power.setKeepAwake(false);
            return;
        }
        power.setKeepAwake(true);
        keepAwakeUntil = minutes > 0 ? Date.now() + minutes * 60000 : 0;
        if (minutes > 0) {
            keepAwakeTimer.interval = minutes * 60000;
            keepAwakeTimer.restart();
        } else {
            keepAwakeTimer.stop();
        }
    }

    onKeepingAwakeChanged: {
        if (!keepingAwake) {
            keepAwakeUntil = 0;
            keepAwakeTimer.stop();
        }
    }

    Timer {
        id: keepAwakeTimer
        onTriggered: quick.keepAwake(-1)
    }

    readonly property string reconfigureKWin: "dbus-send --session --type=method_call --dest=org.kde.KWin /KWin org.kde.KWin.reconfigure"

    // On means warmer colours on the screen right now, which is what the tile
    // shows: a schedule that is waiting for the evening counts as off.
    readonly property bool nightLightOn: power !== null && power.nightLightAvailable
        && power.nightLightEnabled && !power.nightLightInhibited && power.nightLightRunning

    // KWin's night light modes as it reports them (0 and 1 follow sunset
    // where you are, 2 a set time, 3 always on) and as kwinrc spells them.
    readonly property int nightLightAlwaysOn: 3
    readonly property var nightLightModeNames: ["Automatic", "Location", "Times", "Constant"]

    function setNightLightMode(mode: int): void {
        run("kwriteconfig6 --notify --file kwinrc --group NightColor --key Mode " + nightLightModeNames[mode] + "; " + reconfigureKWin);
    }

    // KWin can only pause night light, not start it early, so the tile does
    // what Windows' does by switching to "always on" until it is turned off
    // again, then puts the schedule back. Turning it on for the first time
    // keeps "always on", so the tile works straight away.
    function setNightLight(on: bool): void {
        if (!power || !power.nightLightAvailable) {
            return;
        }
        if (on) {
            if (power.nightLightInhibitedHere) {
                // Unpausing is enough in the evening; by day the schedule
                // keeps it off, which the check after it settles catches.
                power.toggleNightLightInhibition();
                nightLightStart.restart();
            } else if (!power.nightLightEnabled) {
                run("kreadconfig6 --file kwinrc --group NightColor --key Mode | grep -q . "
                    + "|| kwriteconfig6 --notify --file kwinrc --group NightColor --key Mode Constant; "
                    + "kwriteconfig6 --notify --file kwinrc --group NightColor --key Active --type bool true; "
                    + reconfigureKWin);
            } else {
                keepNightLightOn();
            }
            return;
        }
        const schedule = Plasmoid.configuration.nightLightScheduleMode;
        if (power.nightLightMode === nightLightAlwaysOn && schedule >= 0) {
            // Back on the schedule; if the schedule says evening it would
            // stay on, so it is paused until the next change instead.
            Plasmoid.configuration.nightLightScheduleMode = -1;
            setNightLightMode(schedule);
            nightLightSettle.restart();
        } else if (power.nightLightMode === nightLightAlwaysOn) {
            run("kwriteconfig6 --notify --file kwinrc --group NightColor --key Active --type bool false; " + reconfigureKWin);
        } else if (!power.nightLightInhibitedHere) {
            power.toggleNightLightInhibition();
        }
    }

    function keepNightLightOn(): void {
        if (power && !power.nightLightRunning && power.nightLightMode !== nightLightAlwaysOn) {
            Plasmoid.configuration.nightLightScheduleMode = power.nightLightMode;
            setNightLightMode(nightLightAlwaysOn);
        }
    }

    Timer {
        id: nightLightStart
        interval: 1500
        onTriggered: quick.keepNightLightOn()
    }

    Timer {
        id: nightLightSettle
        interval: 1500
        onTriggered: {
            if (quick.power && quick.power.nightLightRunning && !quick.power.nightLightInhibited) {
                quick.power.toggleNightLightInhibition();
            }
        }
    }

    // A translated message can hold an apostrophe, which would end the
    // shell's single quotes.
    function shellQuote(text: string): string {
        return "'" + text.replace(/'/g, "'\\''") + "'";
    }

    // Wild OS Settings takes KDE module names as page names too, so
    // "kcm_bluetooth" lands on Bluetooth. Only plain ids (letters, digits,
    // underscores and hyphens) are passed, so nothing reaches the shell.
    function openSettings(module: string): void {
        const page = /^[A-Za-z0-9_-]+$/.test(module) ? " --page " + module : "";
        // Settings ships with Wild OS, but if it has gone the click still
        // says something rather than doing nothing.
        run("if command -v wildos-settings >/dev/null; then setsid -f wildos-settings" + page + " >/dev/null 2>&1; "
            + "else notify-send -a 'Wild OS' -i preferences-system " + shellQuote(qsTr("Settings is missing")) + " "
            + shellQuote(qsTr("Reinstall it from the Store to change this.")) + "; fi");
        quick.expanded = false;
    }

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: "preferences-system"
    Plasmoid.title: qsTr("Quick settings")
    // One plain line per part, as Windows shows over its tray button.
    readonly property var tooltipLines: {
        const lines = [networkTooltip];
        if (!audio || !audio.available) {
            lines.push(qsTr("Sound: No audio output"));
        } else {
            lines.push(audio.muted ? qsTr("Speakers: Muted") : qsTr("Speakers: %1%").arg(audio.percent));
        }
        if (power && power.hasBattery) {
            lines.push(power.fullyCharged ? qsTr("Battery: Fully charged")
                : power.charging ? qsTr("Battery: %1%, charging").arg(power.percent)
                : qsTr("Battery: %1% remaining").arg(power.percent));
        }
        return lines;
    }

    toolTipMainText: tooltipLines.join(", ")
    toolTipItem: Column {
        padding: 8
        spacing: 2

        Repeater {
            model: quick.tooltipLines

            UI.Label {
                required property string modelData
                text: modelData
                size: 12
            }
        }
    }

    preferredRepresentation: compactRepresentation
    compactRepresentation: TrayButton {
        quick: quick
    }
    fullRepresentation: Flyout {
        quick: quick
    }

    Component.onCompleted: {
        if (Plasmoid.hasOwnProperty("activationTogglesExpanded")) {
            Plasmoid.activationTogglesExpanded = true;
        }
    }
}
