import QtQuick
import QtQuick.Layouts
import org.kde.notificationmanager as NotificationManager
import WildOS.Settings
import WildOS.UI as UI

// Notifications, after Windows 11: do not disturb and how alerts behave.
// These are the notification server's own settings, shared with the clock
// and quick settings flyouts, so all of them agree.
PageScroll {
    id: page

    property real now: Date.now()

    NotificationManager.Settings {
        id: settings
        live: true
    }

    // A timed do not disturb ends on its own.
    Timer {
        interval: 60000
        running: true
        repeat: true
        onTriggered: page.now = Date.now()
    }

    readonly property bool doNotDisturb: {
        const until = settings.notificationsInhibitedUntil;
        const timed = until !== undefined && until !== null && !isNaN(until.getTime()) && now < until.getTime();
        return timed || settings.notificationsInhibitedByApplication;
    }

    function setDoNotDisturb(on) {
        if (on) {
            // As in Plasma, a year from now stands for "until switched off".
            const until = new Date();
            until.setFullYear(until.getFullYear() + 1);
            settings.notificationsInhibitedUntil = until;
        } else {
            settings.notificationsInhibitedUntil = undefined;
            settings.revokeApplicationInhibitions();
        }
        save();
    }

    function save() {
        settings.save();
        now = Date.now();
    }

    // Quiet hours live in wildosrc; the taskbar's quick settings checks them
    // once a minute and turns do not disturb on until the end time, so it
    // still ends on time if Settings is closed.
    property int quietRevision: 0
    readonly property bool quietHours: { quietRevision; return System.config("wildosrc", "QuietHours", "Enabled", false); }
    readonly property string quietFrom: { quietRevision; return System.config("wildosrc", "QuietHours", "From", "2200"); }
    readonly property string quietTo: { quietRevision; return System.config("wildosrc", "QuietHours", "To", "0700"); }

    function setQuiet(key, value) {
        System.setConfig("wildosrc", "QuietHours", key, value);
        quietRevision++;
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
                iconName: "bell"
                title: qsTr("Do not disturb")
                description: qsTr("Notifications are kept in the list instead of popping up")
                status: (page.doNotDisturb) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.doNotDisturb
                    Accessible.name: qsTr("Do not disturb")
                    onToggled: page.setDoNotDisturb(checked)
                }
            }

            UI.SettingRow {
                title: qsTr("Turn on do not disturb automatically at set times")
                description: qsTr("Quiet hours, for example overnight. You can still turn it off early")
                status: (page.quietHours) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.quietHours
                    Accessible.name: qsTr("Turn on do not disturb automatically at set times")
                    onToggled: page.setQuiet("Enabled", checked)
                }
            }

            UI.SettingRow {
                visible: page.quietHours
                title: qsTr("From")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Quiet hours start")
                    model: page.hours()
                    readonly property var choice: page.quietFrom
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => page.setQuiet("From", model[index].value)
                }
            }

            UI.SettingRow {
                visible: page.quietHours
                title: qsTr("To")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Quiet hours end")
                    model: page.hours()
                    readonly property var choice: page.quietTo
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => page.setQuiet("To", model[index].value)
                }
            }

            UI.SettingRow {
                title: qsTr("Turn on do not disturb automatically when sharing or mirroring the screen")
                description: qsTr("So alerts never show during a presentation")
                status: (settings.inhibitNotificationsWhenScreensMirrored) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: settings.inhibitNotificationsWhenScreensMirrored
                    Accessible.name: qsTr("Turn on do not disturb automatically when sharing or mirroring the screen")
                    onToggled: {
                        settings.inhibitNotificationsWhenScreensMirrored = checked;
                        settings.inhibitNotificationsWhenScreenSharing = checked;
                        page.save();
                    }
                }
            }

            UI.SettingRow {
                title: qsTr("Let urgent notifications through")
                description: qsTr("Low battery and other critical alerts still pop up during do not disturb")
                status: (settings.criticalPopupsInDoNotDisturbMode) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: settings.criticalPopupsInDoNotDisturbMode
                    Accessible.name: qsTr("Let urgent notifications through")
                    onToggled: {
                        settings.criticalPopupsInDoNotDisturbMode = checked;
                        page.save();
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
                title: qsTr("Play sounds")
                description: qsTr("Apps can play a sound with their notifications")
                status: (!settings.notificationSoundsInhibited) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: !settings.notificationSoundsInhibited
                    Accessible.name: qsTr("Play sounds")
                    onToggled: {
                        settings.notificationSoundsInhibited = !checked;
                        page.save();
                    }
                }
            }

            UI.SettingRow {
                title: qsTr("Show badges on taskbar apps")
                description: qsTr("A count of unread notifications on the app's icon")
                status: (settings.badgesInTaskManager) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: settings.badgesInTaskManager
                    Accessible.name: qsTr("Show badges on taskbar apps")
                    onToggled: {
                        settings.badgesInTaskManager = checked;
                        page.save();
                    }
                }
            }

            UI.SettingRow {
                title: qsTr("Keep notifications on screen for")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Keep notifications on screen for")
                    model: [
                        { value: 5000, text: qsTr("5 seconds") },
                        { value: 7000, text: qsTr("7 seconds") },
                        { value: 15000, text: qsTr("15 seconds") },
                        { value: 30000, text: qsTr("30 seconds") },
                        { value: 60000, text: qsTr("1 minute") }
                    ].concat([5000, 7000, 15000, 30000, 60000].includes(settings.popupTimeout) ? [] : [{ value: settings.popupTimeout, text: qsTr("%1 seconds").arg(Math.round(settings.popupTimeout / 1000)) }])
                    readonly property var choice: settings.popupTimeout
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => {
                        const value = model[index].value;
                        settings.popupTimeout = value;
                        page.save();
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
                visible: Pages.exists("appnotifications")
                title: Pages.page("appnotifications").title
                description: Pages.page("appnotifications").description
                chevron: true
                onClicked: Navigator.open("appnotifications")
            }
        }
    }
}
