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
    // sdwdate is the one setting the clock, and a time server was not chosen.
    readonly property bool throughTor: Clock.secureTime && Clock.timeSource === "tor"
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
            autoToggle.checked = Qt.binding(() => Clock.automatic || page.throughTor);
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
                    text: qsTr("%1 %2").arg(Format.longDate(page.now)).arg(page.now.getFullYear())
                    size: 15
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: page.throughTor ? (Clock.secureTimeSet ? qsTr("Set automatically through Tor") : qsTr("Being set through Tor, which can take a few minutes after start-up"))
                        : Clock.automatic ? (Clock.synchronised ? qsTr("Set automatically from the internet") : qsTr("Set automatically when the computer is online"))
                        : qsTr("Set by hand")
                    color: UI.Style.inkMuted
                }
            }
        }
    }

    StatusCard {
        visible: page.error.length > 0 || !Clock.available
        iconName: "warning"
        warning: true
        title: Clock.available ? qsTr("The clock was not changed") : qsTr("The clock cannot be changed here")
        description: Clock.available ? page.error : qsTr("The service that sets the date and time is not running on this computer.")
    }

    UI.Card {
        Layout.fillWidth: true
        enabled: Clock.available

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "clock"
                title: qsTr("Set time automatically")
                // sdwdate asks onion sites through Tor, so none of them sees
                // this computer's address, and no time server is asked.
                description: page.throughTor ? qsTr("Wild OS keeps the clock right by asking several sites through Tor, so none of them sees your address")
                    : Clock.canAutomatic ? qsTr("Keeps the clock right using a time server on the internet")
                    : qsTr("No time service is installed, so the clock is set by hand")
                enabled: Clock.canAutomatic && !page.throughTor
                status: Clock.automatic || page.throughTor ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    id: autoToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Clock.automatic || page.throughTor
                    Accessible.name: qsTr("Set time automatically")
                    onToggled: Clock.setAutomatic(checked)
                }
            }

            UI.SettingRow {
                iconName: "shield"
                title: qsTr("Set the time from")
                description: Clock.timeSource === "server"
                    ? qsTr("A time server sets the clock within seconds of going online. It sees this computer's internet address.")
                    : qsTr("Sites reached through Tor set the clock a few minutes after start-up. None of them sees your address.")
                enabled: !Clock.sourceBusy

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Set the time from")
                    model: [
                        { value: "tor", text: qsTr("Tor (private)") },
                        { value: "server", text: qsTr("A time server (faster)") }
                    ]
                    readonly property string choice: Clock.timeSource
                    currentIndex: model.findIndex(o => o.value === choice)
                    onActivated: index => Clock.setTimeSource(model[index].value, Clock.timeServer)
                }
            }

            UI.SettingRow {
                visible: Clock.timeSource === "server"
                iconName: "globe"
                title: qsTr("Time server")
                description: qsTr("Leave empty to use Debian's time servers")
                enabled: !Clock.sourceBusy

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    UI.TextField {
                        id: serverField
                        width: 220
                        text: Clock.timeServer
                        placeholderText: qsTr("For example time.cloudflare.com")
                        Accessible.name: qsTr("Time server")
                        error: !Clock.serverNameValid(text.trim())
                        onAccepted: if (useServer.enabled) useServer.clicked()
                    }

                    UI.Button {
                        id: useServer
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Use")
                        enabled: serverField.text.trim() !== Clock.timeServer && Clock.serverNameValid(serverField.text.trim())
                        onClicked: Clock.setTimeSource("server", serverField.text.trim())
                    }
                }
            }

            UI.SettingRow {
                id: zoneRow
                iconName: "globe"
                title: qsTr("Time zone")

                readonly property var zones: Clock.timeZones()
                readonly property string choice: Clock.timeZone
                // The zones the search box leaves in the list. The one in
                // use stays, so the drop-down always has it to show.
                readonly property var shown: {
                    const words = zoneSearch.text.toLowerCase().split(/\s+/).filter(word => word.length > 0);
                    if (words.length === 0) {
                        return zones;
                    }
                    return zones.filter(zone => {
                        const names = (zone.text + " " + zone.value.replace(/_/g, " ")).toLowerCase();
                        return zone.value === choice || words.every(word => names.includes(word));
                    });
                }

                below: UI.SearchField {
                    id: zoneSearch
                    width: Math.min(260, parent.width)
                    placeholderText: qsTr("Search time zones")
                    // Enter or Down opens the list as the search left it. The
                    // list only takes arrows and Enter while its box has focus.
                    onAccepted: { zoneBox.forceActiveFocus(); zoneBox.popup.open(); }
                    onDownPressed: { zoneBox.forceActiveFocus(); zoneBox.popup.open(); }
                }

                UI.ComboBox {
                    id: zoneBox
                    anchors.verticalCenter: parent.verticalCenter
                    // As wide as the whole list needs, so it does not
                    // shrink and grow while the search narrows it.
                    property real fullWidth: 0
                    width: Math.max(implicitWidth, fullWidth)
                    onImplicitWidthChanged: fullWidth = Math.max(fullWidth, implicitWidth)
                    Accessible.name: qsTr("Time zone")
                    model: zoneRow.shown
                    currentIndex: model ? model.findIndex(o => o.value === zoneRow.choice) : -1
                    onActivated: index => {
                        Clock.setTimeZone(model[index].value);
                        zoneSearch.text = "";
                    }
                }
            }

            UI.SettingRow {
                iconName: "pencil"
                title: qsTr("Set the date and time manually")
                description: Clock.automatic ? qsTr("Turn off automatic time first") : ""
                enabled: !Clock.automatic

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Change")
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
                title: qsTr("Clock")
                description: qsTr("Show times on the 12-hour or 24-hour clock, whatever your regional format")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Clock")
                    model: [
                        { value: 0, text: qsTr("Same as regional format") },
                        { value: 12, text: qsTr("12-hour (1:30 PM)") },
                        { value: 24, text: qsTr("24-hour (13:30)") }
                    ]
                    readonly property int choice: UI.TimeFormat.hours
                    currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                    onActivated: index => System.setConfig("wildosrc", qsTr("Clock"), "Hours", model[index].value)
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

        title: qsTr("Change the date and time")
        text: Format.twelveHour() ? qsTr("Type the date as day, month and year, and the time with AM or PM.")
            : qsTr("Type the date as day, month and year, and the time on the 24-hour clock.")
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
                placeholderText: qsTr("Date, for example 07/11/2026")
                error: text.length > 0 && changeDialog.dateParts === null
                onAccepted: if (changeDialog.valid) changeButton.clicked()
            }

            UI.TextField {
                id: timeField
                Layout.preferredWidth: 110
                placeholderText: qsTr("Time")
                error: text.length > 0 && changeDialog.hour < 0
                onAccepted: if (changeDialog.valid) changeButton.clicked()
            }
        }

        buttons: [
            UI.Button {
                id: changeButton
                text: qsTr("Change")
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
                text: qsTr("Cancel")
                onClicked: changeDialog.close()
            }
        ]
    }
}
