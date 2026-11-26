import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Night light, after Windows 11: the switch, how warm, and when. The
// settings are KWin's own (kwinrc, NightColor), which it rereads when asked.
PageScroll {
    id: page

    property int revision: 0
    readonly property bool active: { revision; return System.config("kwinrc", "NightColor", "Active", false); }
    // KWin spells the schedule by name; older Wild OS wrote its number.
    readonly property var modeNames: ["Automatic", "Location", "Times", "Constant"]
    readonly property int mode: {
        revision;
        const stored = String(System.config("kwinrc", "NightColor", "Mode", ""));
        const named = modeNames.indexOf(stored);
        return named >= 0 ? named : (parseInt(stored) || 0);
    }
    readonly property int temperature: { revision; return System.config("kwinrc", "NightColor", "NightTemperature", 4500); }
    readonly property string evening: { revision; return System.config("kwinrc", "NightColor", "EveningBeginFixed", "1800"); }
    readonly property string morning: { revision; return System.config("kwinrc", "NightColor", "MorningBeginFixed", "0600"); }

    // Warmest is 1900K; the strength slider runs from daylight to that.
    readonly property int warmest: 1900
    readonly property int neutral: 6500

    function set(key, value) {
        System.setConfig("kwinrc", "NightColor", key, value);
        System.reloadKWin();
        revision++;
    }

    function hours() {
        const list = [];
        for (let h = 0; h < 24; ++h) {
            for (const m of [0, 30]) {
                const value = (h < 10 ? "0" : "") + h + (m === 0 ? "00" : "30");
                list.push({ value: value, text: Format.time(new Date(2000, 0, 1, h, m)) });
            }
        }
        return list;
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "moon"
                title: qsTr("Night light")
                description: qsTr("Shows warmer colours to help you sleep")
                status: (page.active) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.active
                    Accessible.name: qsTr("Night light")
                    onToggled: page.set("Active", checked)
                }
            }

            UI.SettingRow {
                id: strengthRow
                title: qsTr("Strength")
                enabled: page.active
                // The slider's own value while it is dragged, so the number
                // follows the pointer instead of waiting for the saved one.
                status: qsTr("%1%").arg(strength.pressed ? Math.round(strength.value) : Math.round((page.neutral - page.temperature) / (page.neutral - page.warmest) * 100))

                UI.Slider {
                    id: strength
                    anchors.verticalCenter: parent.verticalCenter
                    width: 240
                    accessibleName: qsTr("Strength")
                    enabled: page.active
                    from: 0
                    to: 100
                    value: Math.round((page.neutral - page.temperature) / (page.neutral - page.warmest) * 100)
                    onMoved: page.set("NightTemperature", Math.round(page.neutral - value / 100 * (page.neutral - page.warmest)))
                }
            }

            UI.SettingRow {
                title: qsTr("Schedule night light")
                description: page.mode === 3 ? qsTr("Warmer colours all the time")
                    : page.mode === 2 ? qsTr("Between the hours you choose")
                    : qsTr("From sunset to sunrise where you are")
                enabled: page.active

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Schedule night light")
                    enabled: page.active
                    // Sunset needs to know where the computer is, which Wild OS
                    // only works out when location services are on.
                    model: (System.locationServices ? [{ value: 0, text: qsTr("Sunset to sunrise") }] : [])
                        .concat([{ value: 2, text: qsTr("Set hours") }, { value: 3, text: qsTr("All the time") }])
                    readonly property var choice: page.mode === 1 ? 0 : page.mode
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => page.set("Mode", page.modeNames[model[index].value])
                }
            }

            UI.SettingRow {
                visible: page.mode === 2
                title: qsTr("Turn on")
                enabled: page.active

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Turn on")
                    enabled: page.active
                    model: page.hours()
                    readonly property var choice: page.evening
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => page.set("EveningBeginFixed", model[index].value)
                }
            }

            UI.SettingRow {
                visible: page.mode === 2
                title: qsTr("Turn off")
                enabled: page.active

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Turn off")
                    enabled: page.active
                    model: page.hours()
                    readonly property var choice: page.morning
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => page.set("MorningBeginFixed", model[index].value)
                }
            }
        }
    }
}
