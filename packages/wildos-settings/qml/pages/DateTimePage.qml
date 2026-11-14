import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Date and time, after Windows 11: set the time automatically, the time
// zone, and setting the clock by hand when automatic time is off. All of it
// goes through timedated, which asks for the administrator password when
// the account needs one.
PageScroll {
    id: page

    property date now: new Date()
    property string error

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: page.now = new Date()
    }

    Connections {
        target: Clock
        function onFailed(reason) {
            page.error = reason;
            autoToggle.checked = Qt.binding(() => Clock.automatic || Clock.secureTime);
        }
        function onChanged() {
            page.error = "";
        }
    }

    UI.Card {
        Layout.fillWidth: true
        implicitHeight: 112

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 24

            UI.Label {
                text: Format.time(page.now)
                size: 40
                font.weight: UI.Style.weightLight
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: Format.longDate(page.now) + " " + page.now.getFullYear()
                    size: 15
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: Clock.secureTime ? (Clock.secureTimeSet ? "Set automatically through Tor" : "Being set through Tor, which can take a few minutes after start-up")
                        : Clock.automatic ? (Clock.synchronised ? "Set automatically from the internet" : "Set automatically when the computer is online")
                        : "Set by hand"
                    color: UI.Style.inkMuted
                }
            }
        }
    }

    StatusCard {
        visible: page.error.length > 0 || !Clock.available
        iconName: "warning"
        warning: true
        title: Clock.available ? "The clock was not changed" : "The clock cannot be changed here"
        description: Clock.available ? page.error : "The service that sets the date and time is not running on this computer."
    }

    UI.Card {
        Layout.fillWidth: true
        enabled: Clock.available

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "clock"
                title: "Set time automatically"
                // sdwdate asks onion sites through Tor, so none of them sees
                // this computer's address, and no time server is asked.
                description: Clock.secureTime ? "Wild OS keeps the clock right by asking several sites through Tor, so none of them sees your address"
                    : Clock.canAutomatic ? "Keeps the clock right using a time server on the internet"
                    : "No time service is installed, so the clock is set by hand"
                enabled: Clock.canAutomatic && !Clock.secureTime
                status: Clock.automatic || Clock.secureTime ? "On" : "Off"

                UI.Toggle {
                    id: autoToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Clock.automatic || Clock.secureTime
                    Accessible.name: "Set time automatically"
                    onToggled: Clock.setAutomatic(checked)
                }
            }

            UI.SettingRow {
                iconName: "globe"
                title: "Time zone"

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: "Time zone"
                    model: Clock.timeZones()
                    readonly property string choice: Clock.timeZone
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => Clock.setTimeZone(model[index].value)
                }
            }

            UI.SettingRow {
                iconName: "pencil"
                title: "Set the date and time manually"
                description: Clock.automatic ? "Turn off automatic time first" : ""
                enabled: !Clock.automatic

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Change"
                    onClicked: changeDialog.open()
                }
            }
        }
    }

    // Its own card because it is a display choice, kept in wildosrc
    // [Clock] Hours, which works without timedated. WildOS.UI's TimeFormat
    // reads it for the taskbar, the lock screen and Wild OS's apps.
    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "clock"
                title: "Clock"
                description: "Show times on the 12-hour or 24-hour clock, whatever your regional format"

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: "Clock"
                    model: [
                        { value: 0, text: "Same as regional format" },
                        { value: 12, text: "12-hour (1:30 PM)" },
                        { value: 24, text: "24-hour (13:30)" }
                    ]
                    readonly property int choice: UI.TimeFormat.hours
                    currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                    onActivated: index => System.setConfig("wildosrc", "Clock", "Hours", model[index].value)
                }
            }
        }
    }

    UI.Dialog {
        id: changeDialog

        readonly property var dateParts: /^(\d{1,2})\/(\d{1,2})\/(\d{4})$/.exec(dateField.text.trim())
        // "13:30", "1:30 PM", "1.30pm" or "1:30 p.m.", whichever clock is chosen.
        readonly property var timeParts: /^(\d{1,2})[:.](\d{2})(?:\s*([ap])\.?\s*m\.?)?$/i.exec(timeField.text.trim())
        readonly property int hour: {
            if (timeParts === null) {
                return -1;
            }
            const h = +timeParts[1];
            if (timeParts[3] === undefined) {
                return h < 24 ? h : -1;
            }
            if (h < 1 || h > 12) {
                return -1;
            }
            return h % 12 + (timeParts[3].toLowerCase() === "p" ? 12 : 0);
        }
        readonly property bool valid: dateParts !== null && hour >= 0 && +timeParts[2] < 60
            && +dateParts[2] >= 1 && +dateParts[2] <= 12 && +dateParts[1] >= 1 && +dateParts[1] <= 31

        title: "Change the date and time"
        text: Format.twelveHour() ? "Type the date as day, month and year, and the time with AM or PM."
            : "Type the date as day, month and year, and the time on the 24-hour clock."
        onOpened: {
            const d = new Date();
            dateField.text = Format.shortDate(d);
            timeField.text = Format.time(d);
            dateField.forceActiveFocus();
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            UI.TextField {
                id: dateField
                Layout.fillWidth: true
                placeholderText: "Date, for example 07/11/2026"
                error: text.length > 0 && changeDialog.dateParts === null
                onAccepted: if (changeDialog.valid) changeButton.clicked()
            }

            UI.TextField {
                id: timeField
                Layout.preferredWidth: 110
                placeholderText: "Time"
                error: text.length > 0 && changeDialog.hour < 0
                onAccepted: if (changeDialog.valid) changeButton.clicked()
            }
        }

        buttons: [
            UI.Button {
                id: changeButton
                text: "Change"
                kind: "primary"
                enabled: changeDialog.valid
                onClicked: {
                    const d = changeDialog.dateParts;
                    const t = changeDialog.timeParts;
                    Clock.setDateTime(+d[1], +d[2], +d[3], changeDialog.hour, +t[2]);
                    changeDialog.close();
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: changeDialog.close()
            }
        ]
    }
}
