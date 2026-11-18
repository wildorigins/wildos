import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Screen lock: when the screen locks by itself and when the password is
// asked for. These are the screen locker's own settings (kscreenlockerrc),
// which it rereads when told to.
PageScroll {
    id: page

    property int revision: 0
    readonly property bool autoLock: { revision; return System.config("kscreenlockerrc", "Daemon", "Autolock", true); }
    readonly property int timeout: { revision; return System.config("kscreenlockerrc", "Daemon", "Timeout", 5); }
    readonly property bool lockOnResume: { revision; return System.config("kscreenlockerrc", "Daemon", "LockOnResume", true); }
    readonly property int grace: { revision; return System.config("kscreenlockerrc", "Daemon", "LockGrace", 5); }

    function set(key, value) {
        System.setConfig("kscreenlockerrc", "Daemon", key, value);
        System.reloadScreenLocker();
        revision++;
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "lock"
                title: qsTr("Lock the screen after")
                description: qsTr("When the computer has not been used for this long")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Lock the screen after")
                    model: [0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60]
                        .concat([0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60].includes(page.timeout) ? [] : [page.timeout])
                        .map(m => ({ value: m, text: m === 0 ? qsTr("Never") : m === 60 ? qsTr("1 hour") : (m === 1 ? qsTr("1 minute") : qsTr("%1 minutes").arg(m)) }))
                    readonly property var choice: page.autoLock ? page.timeout : 0
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => {
                        const value = model[index].value;
                        if (value === 0) {
                            page.set("Autolock", false);
                        } else {
                            System.setConfig("kscreenlockerrc", "Daemon", "Autolock", true);
                            page.set("Timeout", value);
                        }
                    }
                }
            }

            UI.SettingRow {
                title: qsTr("Lock after waking from sleep")
                description: qsTr("Ask for your password when the computer wakes up")
                status: (page.lockOnResume) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.lockOnResume
                    Accessible.name: qsTr("Lock after waking from sleep")
                    onToggled: page.set("LockOnResume", checked)
                }
            }

            UI.SettingRow {
                title: qsTr("Ask for the password")
                description: qsTr("How soon after the screen locks your password is needed")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Ask for the password")
                    model: [0, 5, 10, 30, 60]
                        .concat([0, 5, 10, 30, 60].includes(page.grace) ? [] : [page.grace])
                        .map(s => ({ value: s, text: s === 0 ? qsTr("Straight away") : s === 60 ? qsTr("After 1 minute") : qsTr("After %1 seconds").arg(s) }))
                    readonly property var choice: page.grace
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => page.set("LockGrace", model[index].value)
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "image"
                title: qsTr("Lock screen picture")
                description: qsTr("The picture behind the clock")
                chevron: true
                onClicked: Navigator.open("lockscreen")
            }
        }
    }
}
