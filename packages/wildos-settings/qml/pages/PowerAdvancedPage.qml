import QtQuick
import QtQuick.Layouts
import org.kde.plasma.private.battery
import WildOS.Settings
import WildOS.UI as UI

// More power settings, after Windows' "Lid and power button controls": what
// closing the lid and pressing the power button do, when the screen dims,
// and what happens when the battery is nearly flat. These are powerdevil's
// own settings in powerdevilrc, under the keys its settings module writes,
// and powerdevil rereads them when told to. Turning the screen off and
// sleeping are on Power and battery, so they are not repeated here.
PageScroll {
    id: page

    Component.onCompleted: PowerOptions.refresh()

    BatteryControlModel {
        id: battery
    }

    readonly property bool hasBattery: battery.hasInternalBatteries && battery.hasCumulative

    property int revision: 0

    // powerdevil's PowerButtonAction values (powerdevilenums.h).
    readonly property int doNothing: 0
    readonly property int sleep: 1
    readonly property int hibernate: 2
    readonly property int shutDown: 8
    readonly property int askWhatToDo: 16
    readonly property int lock: 32
    readonly property int screenOff: 64
    readonly property int screenToggle: 128

    readonly property var profiles: hasBattery
        ? [{ id: "Battery", power: qsTr("On battery power, pressing the power button will"), lid: qsTr("On battery power, closing the lid will"), dim: qsTr("On battery power, dim my screen after") },
           { id: "AC", power: qsTr("When plugged in, pressing the power button will"), lid: qsTr("When plugged in, closing the lid will"), dim: qsTr("When plugged in, dim my screen after") }]
        : [{ id: "AC", power: qsTr("Pressing the power button will"), lid: qsTr("Closing the lid will"), dim: qsTr("Dim my screen after") }]

    function actionText(value) {
        switch (value) {
        case page.doNothing: return qsTr("Do nothing");
        case page.sleep: return qsTr("Sleep");
        case page.hibernate: return qsTr("Hibernate");
        case page.shutDown: return qsTr("Shut down");
        case page.askWhatToDo: return qsTr("Ask me what to do");
        case page.lock: return qsTr("Lock");
        case page.screenOff: return qsTr("Turn off the screen");
        case page.screenToggle: return qsTr("Turn the screen off or on");
        }
        return qsTr("Something else");
    }

    // The same choices powerdevil's own settings offer for each, without
    // sleep or hibernate where this computer cannot do them. A value set
    // elsewhere that is not on the list still shows, so nothing is changed
    // just by opening the page.
    function actionChoices(values, current) {
        const list = values.filter(v => (v !== page.sleep || PowerOptions.canSuspend) && (v !== page.hibernate || PowerOptions.canHibernate));
        if (!list.includes(current)) {
            list.push(current);
        }
        return list.map(v => ({ value: v, text: page.actionText(v) }));
    }

    // powerdevil's defaults (powerdevilsettingsdefaults.cpp) for keys nobody
    // has set yet.
    function lidAction(profile) {
        revision;
        return System.config("powerdevilrc", profile + "/SuspendAndShutdown", "LidAction", PowerOptions.canSuspend ? page.sleep : page.screenOff);
    }

    function powerButtonAction(profile) {
        revision;
        return System.config("powerdevilrc", profile + "/SuspendAndShutdown", "PowerButtonAction", page.askWhatToDo);
    }

    // The low battery profile is powerdevil's third, used below the low
    // battery level. Wild OS shows two like Windows, so the battery choice
    // covers it too and the lid never behaves differently at 9%.
    function setButtonAction(profile, key, value) {
        System.setConfig("powerdevilrc", profile + "/SuspendAndShutdown", key, value);
        if (profile === "Battery") {
            System.setConfig("powerdevilrc", "LowBattery/SuspendAndShutdown", key, value);
        }
        System.reloadPowerManagement();
        revision++;
    }

    readonly property bool lidWithScreen: {
        revision;
        return !System.config("powerdevilrc", "AC/SuspendAndShutdown", "InhibitLidActionWhenExternalMonitorPresent", true);
    }

    function setLidWithScreen(on) {
        for (const profile of ["AC", "Battery", "LowBattery"]) {
            System.setConfig("powerdevilrc", profile + "/SuspendAndShutdown", "InhibitLidActionWhenExternalMonitorPresent", !on);
        }
        System.reloadPowerManagement();
        revision++;
    }

    readonly property var dimDefaults: ({ "AC": 300, "Battery": 120 })
    readonly property var screenOffDefaults: ({ "AC": 600, "Battery": 300 })

    function dimAfter(profile) {
        revision;
        if (!System.config("powerdevilrc", profile + "/Display", "DimDisplayWhenIdle", true)) {
            return 0;
        }
        return System.config("powerdevilrc", profile + "/Display", "DimDisplayIdleTimeoutSec", dimDefaults[profile]);
    }

    function screenOffAfter(profile) {
        revision;
        if (!System.config("powerdevilrc", profile + "/Display", "TurnOffDisplayWhenIdle", true)) {
            return 0;
        }
        return System.config("powerdevilrc", profile + "/Display", "TurnOffDisplayIdleTimeoutSec", screenOffDefaults[profile]);
    }

    function setDimAfter(profile, seconds) {
        System.setConfig("powerdevilrc", profile + "/Display", "DimDisplayWhenIdle", seconds > 0);
        if (seconds > 0) {
            System.setConfig("powerdevilrc", profile + "/Display", "DimDisplayIdleTimeoutSec", seconds);
        }
        System.reloadPowerManagement();
        revision++;
    }

    function dimTimeouts(current) {
        const list = [{ value: 30, text: qsTr("30 seconds") }].concat([1, 2, 3, 5, 10, 15, 20, 30].map(m => ({ value: m * 60, text: Format.count(m, qsTr("1 minute"), qsTr("%n minutes", "", m)) })));
        if (current > 0 && !list.some(o => o.value === current)) {
            list.push({ value: current, text: current < 60 ? Format.count(current, qsTr("1 second"), qsTr("%n seconds", "", current)) : Format.count(Math.round(current / 60), qsTr("1 minute"), qsTr("%n minutes", "", Math.round(current / 60))) });
            list.sort((a, b) => a.value - b.value);
        }
        return [{ value: 0, text: qsTr("Never") }].concat(list);
    }

    readonly property int criticalAction: {
        revision;
        const fallback = PowerOptions.canHibernate ? page.hibernate : PowerOptions.canSuspend ? page.sleep : page.doNothing;
        return System.config("powerdevilrc", "BatteryManagement", "BatteryCriticalAction", fallback);
    }

    readonly property int lowLevel: {
        revision;
        return System.config("powerdevilrc", "BatteryManagement", "BatteryLowLevel", 10);
    }

    readonly property int criticalLevel: {
        revision;
        return System.config("powerdevilrc", "BatteryManagement", "BatteryCriticalLevel", 5);
    }

    function setBattery(key, value) {
        System.setConfig("powerdevilrc", "BatteryManagement", key, value);
        System.reloadPowerManagement();
        revision++;
    }

    // Critical has to stay below low, or the low battery warning never shows.
    function criticalLevels(current) {
        const list = [2, 3, 4, 5, 6, 7, 8, 9, 10, 15].filter(n => n < page.lowLevel);
        if (!list.includes(current)) {
            list.push(current);
            list.sort((a, b) => a - b);
        }
        return list.map(n => ({ value: n, text: qsTr("%1%").arg(n) }));
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: PowerOptions.lidPresent ? qsTr("Lid and power button") : qsTr("Power button")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.profiles

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
                            id: powerRow
                            readonly property string label: block.modelData.power
                            iconName: "power"
                            title: label

                            UI.ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: powerRow.label
                                readonly property var choice: page.powerButtonAction(block.modelData.id)
                                model: page.actionChoices([page.doNothing, page.sleep, page.hibernate, page.shutDown, page.lock, page.askWhatToDo, page.screenOff], choice)
                                currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                                onActivated: index => page.setButtonAction(block.modelData.id, "PowerButtonAction", model[index].value)
                            }
                        }

                        UI.SettingRow {
                            id: lidRow
                            readonly property string label: block.modelData.lid
                            visible: PowerOptions.lidPresent
                            iconName: "computer"
                            title: label

                            UI.ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                Accessible.name: lidRow.label
                                readonly property var choice: page.lidAction(block.modelData.id)
                                model: page.actionChoices([page.doNothing, page.sleep, page.hibernate, page.shutDown, page.lock, page.screenOff], choice)
                                currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                                onActivated: index => page.setButtonAction(block.modelData.id, "LidAction", model[index].value)
                            }
                        }
                    }
                }

                UI.SettingRow {
                    visible: PowerOptions.lidPresent
                    iconName: "monitor"
                    title: qsTr("Do this even when another screen is connected")
                    description: qsTr("When this is off, closing the lid with a monitor plugged in leaves the computer running on the monitor")
                    status: page.lidWithScreen ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.lidWithScreen
                        Accessible.name: qsTr("Do this even when another screen is connected")
                        onToggled: page.setLidWithScreen(checked)
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Screen dimming")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.profiles

                    UI.SettingRow {
                        id: dimRow
                        required property var modelData
                        readonly property string label: modelData.dim
                        readonly property int dim: page.dimAfter(modelData.id)
                        readonly property int off: page.screenOffAfter(modelData.id)
                        iconName: "sun"
                        title: label
                        description: dim > 0 && off > 0 && dim >= off ? qsTr("The screen turns off before then, so it won't dim first") : ""
                        descriptionColor: UI.Style.warning

                        UI.ComboBox {
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: dimRow.label
                            model: page.dimTimeouts(dimRow.dim)
                            readonly property var choice: dimRow.dim
                            currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                            onActivated: index => page.setDimAfter(dimRow.modelData.id, model[index].value)
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        visible: page.hasBattery

        SectionTitle {
            text: qsTr("Battery")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "battery"
                    title: qsTr("Critical battery level")
                    description: qsTr("Below this, Wild OS acts before the battery runs out")

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Critical battery level")
                        readonly property var choice: page.criticalLevel
                        model: page.criticalLevels(choice)
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => page.setBattery("BatteryCriticalLevel", model[index].value)
                    }
                }

                UI.SettingRow {
                    iconName: "alert"
                    title: qsTr("When the battery is critically low")

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("When the battery is critically low")
                        readonly property var choice: page.criticalAction
                        model: page.actionChoices([page.doNothing, page.sleep, page.hibernate, page.shutDown], choice)
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => page.setBattery("BatteryCriticalAction", model[index].value)
                    }
                }
            }
        }
    }
}
