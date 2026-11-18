import QtQuick
import org.kde.plasma.private.battery
import org.kde.plasma.private.brightnesscontrolplugin
import org.kde.plasma.private.batterymonitor as BatteryMonitor
import org.kde.plasma.plasma5support as P5Support

// Battery state from Plasma's battery model and screen brightness and night
// light from powerdevil and KWin, through the plugins the stock Power and
// Brightness applets use.
Item {
    id: power

    readonly property bool hasBattery: battery.hasInternalBatteries && battery.hasCumulative
    readonly property int percent: battery.percent
    readonly property bool pluggedIn: battery.pluggedIn
    readonly property bool charging: battery.state === BatteryControlModel.Charging
    readonly property bool fullyCharged: battery.pluggedIn && battery.state === BatteryControlModel.FullyCharged
    readonly property bool notCharging: battery.pluggedIn && battery.state === BatteryControlModel.NoCharge
    readonly property real remainingMsec: battery.smoothedRemainingMsec

    // Only a built-in screen gets the slider: external monitors dimmed over
    // DDC are slow and often refuse, and Windows leaves them out too. A
    // built-in panel is one the kernel lists as a backlight.
    property bool hasBacklight: false
    readonly property bool brightnessAvailable: hasBacklight && screenBrightness.isBrightnessAvailable && primaryDisplay !== null
    property QtObject primaryDisplay: null
    readonly property int brightnessPercent: primaryDisplay && primaryDisplay.maxBrightness > 0
        ? Math.round(primaryDisplay.brightness / primaryDisplay.maxBrightness * 100) : 0

    readonly property bool nightLightAvailable: nightLight.available
    readonly property bool nightLightEnabled: nightLight.enabled
    readonly property bool nightLightInhibited: nightLight.inhibited
    // Whether the schedule has the screen warm right now.
    readonly property bool nightLightRunning: nightLight.running
    readonly property bool nightLightInhibitedHere: nightLight.inhibitedFromApplet
    readonly property int nightLightMode: nightLight.mode

    BatteryControlModel { id: battery }

    P5Support.DataSource {
        engine: "executable"
        connectedSources: ["ls /sys/class/backlight"]
        onNewData: (source, data) => {
            power.hasBacklight = String(data.stdout).trim().length > 0;
            disconnectSource(source);
        }
    }

    // Power mode, from power-profiles-daemon through Plasma's own control:
    // "power-saver", "balanced" and "performance".
    BatteryMonitor.PowerProfilesControl {
        id: profiles
    }

    readonly property bool powerModesAvailable: profiles.isPowerProfileDaemonInstalled && profiles.profiles.length > 0
    readonly property var powerModes: profiles.profiles
    readonly property string powerMode: profiles.activeProfile

    function setPowerMode(mode: string): void {
        profiles.isSilent = true;
        profiles.setProfile(mode);
    }

    ScreenBrightnessControl {
        id: screenBrightness
        // The slider itself is the feedback, so no on-screen display.
        isSilent: true
    }

    NightLightControl { id: nightLight }

    // Keep awake holds powerdevil's manual block, the same one the stock
    // Power applet's "Manually block sleep" sets, so the screen stays on and
    // the computer does not sleep until it is released or the shell exits.
    BatteryMonitor.InhibitionControl { id: inhibition }

    readonly property bool keepingAwake: inhibition.isManuallyInhibited

    function setKeepAwake(on: bool): void {
        if (on && !inhibition.isManuallyInhibited) {
            inhibition.inhibit(qsTr("Keep awake is on in quick settings"));
        } else if (!on && inhibition.isManuallyInhibited) {
            inhibition.uninhibit();
        }
    }

    // The flyout has one brightness slider, for the first display that can be
    // dimmed, which on a laptop is the built-in panel.
    Instantiator {
        id: displays
        model: screenBrightness.displays
        delegate: QtObject {
            required property string displayName
            required property int brightness
            required property int maxBrightness
        }
        onObjectAdded: power.primaryDisplay = displays.objectAt(0)
        onObjectRemoved: power.primaryDisplay = displays.count > 0 ? displays.objectAt(0) : null
    }

    function setBrightnessPercent(value: int): void {
        if (primaryDisplay) {
            screenBrightness.setBrightness(primaryDisplay.displayName,
                Math.max(1, Math.round(value / 100 * primaryDisplay.maxBrightness)));
        }
    }

    function toggleNightLightInhibition(): void {
        nightLight.toggleInhibition();
    }
}
