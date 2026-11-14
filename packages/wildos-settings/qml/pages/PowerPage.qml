import QtQuick
import QtQuick.Layouts
import org.kde.plasma.private.battery
import WildOS.Settings
import WildOS.UI as UI

// Power and battery, after Windows 11: the battery, the power mode, and when
// the screen turns off and the computer sleeps. The mode is powerdevil's
// power profile; the timeouts are its settings (powerdevilrc), which it
// rereads when told to.
PageScroll {
    id: page

    Component.onCompleted: System.refreshPower()

    BatteryControlModel {
        id: battery
    }

    readonly property bool hasBattery: battery.hasInternalBatteries && battery.hasCumulative

    property int revision: 0

    // powerdevil's own defaults, for settings nobody has changed yet.
    readonly property var defaults: ({
        "AC": { screen: 600, sleep: 900 },
        "Battery": { screen: 300, sleep: 600 }
    })

    function screenOff(profile) {
        revision;
        if (!System.config("powerdevilrc", profile + "/Display", "TurnOffDisplayWhenIdle", true)) {
            return 0;
        }
        return System.config("powerdevilrc", profile + "/Display", "TurnOffDisplayIdleTimeoutSec", defaults[profile].screen);
    }

    function sleepAfter(profile) {
        revision;
        // AutoSuspendAction 0 is "do nothing"; anything else sleeps.
        if (System.config("powerdevilrc", profile + "/SuspendAndShutdown", "AutoSuspendAction", 1) === 0) {
            return 0;
        }
        return System.config("powerdevilrc", profile + "/SuspendAndShutdown", "AutoSuspendIdleTimeoutSec", defaults[profile].sleep);
    }

    function setScreenOff(profile, seconds) {
        System.setConfig("powerdevilrc", profile + "/Display", "TurnOffDisplayWhenIdle", seconds > 0);
        if (seconds > 0) {
            System.setConfig("powerdevilrc", profile + "/Display", "TurnOffDisplayIdleTimeoutSec", seconds);
        }
        System.reloadPowerManagement();
        revision++;
    }

    function setSleepAfter(profile, seconds) {
        System.setConfig("powerdevilrc", profile + "/SuspendAndShutdown", "AutoSuspendAction", seconds > 0 ? 1 : 0);
        if (seconds > 0) {
            System.setConfig("powerdevilrc", profile + "/SuspendAndShutdown", "AutoSuspendIdleTimeoutSec", seconds);
        }
        System.reloadPowerManagement();
        revision++;
    }

    function timeouts(current) {
        const minutes = [1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 120, 180, 240, 300];
        const list = minutes.map(m => ({ value: m * 60, text: m < 60 ? Format.plural(m, "minute", "minutes") : Format.plural(m / 60, "hour", "hours") }));
        if (current > 0 && !list.some(o => o.value === current)) {
            list.push({ value: current, text: Format.plural(Math.round(current / 60), "minute", "minutes") });
            list.sort((a, b) => a.value - b.value);
        }
        return [{ value: 0, text: "Never" }].concat(list);
    }

    readonly property var profileNames: ({
        "power-saver": { text: "Best battery life", description: "Saves energy, with apps a little slower" },
        "balanced": { text: "Balanced", description: "A balance of speed and energy use" },
        "performance": { text: "Best performance", description: "As fast as possible, using more energy" }
    })

    function duration(msec) {
        const minutes = Math.round(msec / 60000);
        const h = Math.floor(minutes / 60);
        const m = minutes % 60;
        return h > 0 ? Format.plural(h, "hour", "hours") + (m > 0 ? " " + Format.plural(m, "minute", "minutes") : "") : Format.plural(m, "minute", "minutes");
    }

    StatusCard {
        visible: page.hasBattery
        iconName: battery.pluggedIn ? "plug" : "battery"
        warning: !battery.pluggedIn && battery.percent <= 10
        title: battery.percent + "%"
        description: {
            if (battery.state === BatteryControlModel.FullyCharged && battery.pluggedIn) {
                return "Fully charged";
            }
            if (battery.state === BatteryControlModel.Charging) {
                return battery.smoothedRemainingMsec > 0 ? "Charging, about " + page.duration(battery.smoothedRemainingMsec) + " until full" : "Charging";
            }
            if (battery.pluggedIn) {
                return "Plugged in, not charging";
            }
            return battery.smoothedRemainingMsec > 0 ? "About " + page.duration(battery.smoothedRemainingMsec) + " left" : "On battery";
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: System.powerProfiles.length > 0

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "battery"
                title: "Power mode"
                description: page.profileNames[System.powerProfile] ? page.profileNames[System.powerProfile].description : ""

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: "Power mode"
                    model: System.powerProfiles.map(p => ({ value: p, text: page.profileNames[p] ? page.profileNames[p].text : p }))
                    readonly property var choice: System.powerProfile
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => System.setPowerProfile(model[index].value)
                }
            }
        }
    }

    // Hyper-threading, for the whole PC: Linux apps and games as well as
    // Windows mode. A boot option, so a change offers a restart.
    UI.Card {
        id: threadsCard
        Layout.fillWidth: true
        visible: Processor.threadsAvailable
        readonly property bool pending: Processor.threadsPending

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "chip"
                title: "Use every processor thread"
                description: Processor.everyThreadLocked
                    ? (threadsCard.pending ? "Restart to finish turning it off. " : "")
                        + "Off while extra processor protection is on, in Privacy and security."
                    : threadsCard.pending
                    ? "Restart to finish turning it " + (Processor.everyThread ? "on." : "off.")
                    : "Faster heavy apps. Off keeps the second thread of each core idle, a little safer. Needs a restart."

                UI.Toggle {
                    id: threadsToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Processor.everyThread && !Processor.everyThreadLocked
                    enabled: !Processor.busy && !Processor.everyThreadLocked
                    locked: Processor.everyThreadLocked
                    lockedReason: "Extra processor protection keeps this off"
                    Accessible.name: "Use every processor thread"
                    onToggled: Processor.setEveryThread(checked)
                }
            }

            UI.SettingRow {
                visible: threadsCard.pending
                title: "Restart to apply"
                description: "Open apps are asked to save first."

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Restart now"
                    onClicked: Updates.restart()
                }
            }
        }

        Connections {
            target: Processor
            // A cancelled password leaves the choice as it was, so the switch goes back to it.
            function onChanged() { threadsToggle.checked = Processor.everyThread && !Processor.everyThreadLocked }
            function onFailed(message) { threadsError.text = message }
        }
    }

    UI.Label {
        id: threadsError
        visible: text.length > 0 && Processor.threadsAvailable
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        size: 13
        color: UI.Style.warning
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Screen and sleep"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.hasBattery
                        ? [{ profile: "Battery", when: "On battery power, turn off my screen after", sleep: "On battery power, put my device to sleep after" },
                           { profile: "AC", when: "When plugged in, turn off my screen after", sleep: "When plugged in, put my device to sleep after" }]
                        : [{ profile: "AC", when: "Turn off my screen after", sleep: "Put my device to sleep after" }]

                    Column {
                        id: block
                        required property var modelData
                        required property int index
                        width: parent.width

                        Rectangle {
                            visible: block.index > 0
                            width: parent.width
                            height: 1
                            color: UI.Style.divider
                        }

                        UI.SettingRow {
                            iconName: "display"
                            title: block.modelData.when

                            UI.ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: block.modelData.when
                                model: page.timeouts(page.screenOff(block.modelData.profile))
                                readonly property var choice: page.screenOff(block.modelData.profile)
                                currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                                onActivated: index => page.setScreenOff(block.modelData.profile, model[index].value)
                            }
                        }

                        UI.SettingRow {
                            iconName: "moon"
                            title: block.modelData.sleep

                            UI.ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: block.modelData.sleep
                                model: page.timeouts(page.sleepAfter(block.modelData.profile))
                                readonly property var choice: page.sleepAfter(block.modelData.profile)
                                currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                                onActivated: index => page.setSleepAfter(block.modelData.profile, model[index].value)
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                visible: Pages.exists("poweradvanced")
                title: Pages.page("poweradvanced").title
                description: Pages.page("poweradvanced").description
                chevron: true
                onClicked: Navigator.open("poweradvanced")
            }
        }
    }
}
