import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// Calendar's settings (CalMsSettings.dc), in the window in place of the
// days: the accounts its calendars come from, and the week, events and
// extra calendars.
Rectangle {
    id: page

    property string section: "calendar"

    signal closeRequested()
    signal addAccount()

    radius: UI.Style.radiusPanel
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle
    clip: true

    readonly property var sections: [
        { id: "accounts", name: qsTr("Accounts"), icon: "people", blurb: qsTr("Where your calendars come from") },
        { id: "calendar", name: qsTr("Calendar"), icon: "calendar", blurb: qsTr("Your week, reminders and extra calendars") }
    ]

    Settings {
        id: prefs
        category: "calendar"
        property int firstDay: 1
        property string workDays: "1,2,3,4,5"
        property real workStart: 9
        property real workEnd: 17.5
        property int defaultReminder: 15
        property int defaultLength: 60
    }

    function hourChoices() {
        const list = [];
        for (let h = 0; h < 24; h += 0.5) {
            const whole = Math.floor(h);
            list.push({ value: h, text: Formats.time(new Date(2000, 0, 1, whole, h % 1 ? 30 : 0)) });
        }
        return list;
    }
    readonly property var hours: hourChoices()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.preferredWidth: 236
            Layout.fillHeight: true
            Layout.margins: 10
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                UI.IconButton {
                    iconName: "back"
                    tip: qsTr("Back to calendar")
                    onClicked: page.closeRequested()
                }
                UI.Label {
                    text: qsTr("Settings")
                    size: 16
                    font.weight: UI.Style.weightSemiBold
                }
            }

            Repeater {
                model: page.sections
                delegate: Rectangle {
                    id: nav
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: UI.Style.radiusXs
                    color: page.section === modelData.id ? UI.Style.surfaceSelected : navMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                    MouseArea {
                        id: navMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.section = nav.modelData.id
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 10
                        MailIcon {
                            name: nav.modelData.icon
                            size: 16
                            color: page.section === nav.modelData.id ? UI.Style.iconInk : UI.Style.iconMuted
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: nav.modelData.name
                            size: 13
                            font.weight: page.section === nav.modelData.id ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        }
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 1
            color: UI.Style.borderSubtle
        }

        QQC.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.min(760, parent.width - 56)
                x: 28
                spacing: 14

                Item {
                    implicitHeight: 14
                }
                UI.Label {
                    text: page.sections.find(s => s.id === page.section).name
                    size: 22
                    font.weight: UI.Style.weightSemiBold
                }
                UI.Label {
                    text: page.sections.find(s => s.id === page.section).blurb
                    size: 13
                    color: UI.Style.inkMuted
                }

                // Accounts
                UI.Card {
                    visible: page.section === "accounts"
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("This PC")
                            description: qsTr("Calendars kept only on this computer")
                        }
                        Repeater {
                            model: CalendarEngine.accounts
                            delegate: UI.SettingRow {
                                required property var modelData
                                Layout.fillWidth: true
                                title: modelData.name
                                description: modelData.kind === "google" ? qsTr("Google") : qsTr("Calendar server")
                                UI.Button {
                                    size: "small"
                                    text: qsTr("Remove")
                                    onClicked: removeAccount.ask(modelData)
                                }
                            }
                        }
                    }
                }
                UI.Button {
                    visible: page.section === "accounts"
                    text: qsTr("Add account")
                    iconName: "plus"
                    onClicked: page.addAccount()
                }

                // Calendar: your week
                UI.Label {
                    visible: page.section === "calendar"
                    Layout.topMargin: 4
                    text: qsTr("Your week")
                    size: 13
                    font.weight: UI.Style.weightSemiBold
                }
                UI.Card {
                    visible: page.section === "calendar"
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("First day of the week")
                            UI.ComboBox {
                                readonly property var choices: [{ value: 1, text: Formats.names().dayName(1) }, { value: 0, text: Formats.names().dayName(0) }, { value: 6, text: Formats.names().dayName(6) }]
                                model: choices
                                currentIndex: Math.max(0, choices.findIndex(c => c.value === prefs.firstDay))
                                onActivated: function (i) {
                                    prefs.firstDay = choices[i].value;
                                }
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Working days")
                            description: qsTr("The days Work week shows")
                            Row {
                                spacing: 6
                                Repeater {
                                    model: 7
                                    // A box per day (CalMsSettings.dc): outlined in the
                                    // accent when it's a working day.
                                    delegate: Rectangle {
                                        id: dayBox
                                        required property int index
                                        readonly property int day: (index + prefs.firstDay) % 7
                                        readonly property var days: prefs.workDays.split(",").map(Number)
                                        readonly property bool on: days.indexOf(day) >= 0
                                        width: 32
                                        height: 30
                                        radius: UI.Style.radiusXs
                                        color: on ? UI.Style.accentSoft : dayMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceControl
                                        border.width: on ? 1.5 : 1
                                        border.color: on ? UI.Style.accent : UI.Style.borderSubtle
                                        Accessible.role: Accessible.CheckBox
                                        Accessible.name: Formats.names().dayName(day)
                                        Accessible.checked: on
                                        UI.Label {
                                            anchors.centerIn: parent
                                            text: Formats.names().dayName(dayBox.day, Locale.ShortFormat).charAt(0).toUpperCase()
                                            size: 12
                                            font.weight: dayBox.on ? UI.Style.weightSemiBold : UI.Style.weightRegular
                                            color: dayBox.on ? UI.Style.ink : UI.Style.inkMuted
                                        }
                                        MouseArea {
                                            id: dayMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                let d = dayBox.days.slice();
                                                if (dayBox.on) {
                                                    // A week needs at least one working day.
                                                    if (d.length > 1) {
                                                        d.splice(d.indexOf(dayBox.day), 1);
                                                    }
                                                } else {
                                                    d.push(dayBox.day);
                                                }
                                                prefs.workDays = d.sort().join(",");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Working hours")
                            description: qsTr("Where the week opens each morning")
                            RowLayout {
                                spacing: 8
                                UI.ComboBox {
                                    implicitWidth: Formats.twelveHour() ? 116 : 96
                                    model: page.hours
                                    currentIndex: Math.max(0, page.hours.findIndex(h => h.value === prefs.workStart))
                                    onActivated: function (i) {
                                        prefs.workStart = page.hours[i].value;
                                    }
                                }
                                UI.Label {
                                    text: qsTr("to")
                                    size: 13
                                    color: UI.Style.inkMuted
                                }
                                UI.ComboBox {
                                    implicitWidth: Formats.twelveHour() ? 116 : 96
                                    model: page.hours
                                    currentIndex: Math.max(0, page.hours.findIndex(h => h.value === prefs.workEnd))
                                    onActivated: function (i) {
                                        prefs.workEnd = page.hours[i].value;
                                    }
                                }
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Time zone")
                            description: qsTr("This PC's time zone. Change it in Settings, Time and language.")
                            UI.Label {
                                text: CalendarEngine.timeZoneName
                                size: 13
                            }
                        }
                    }
                }

                // Calendar: events
                UI.Label {
                    visible: page.section === "calendar"
                    Layout.topMargin: 4
                    text: qsTr("Events")
                    size: 13
                    font.weight: UI.Style.weightSemiBold
                }
                UI.Card {
                    visible: page.section === "calendar"
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Default reminder")
                            UI.ComboBox {
                                readonly property var choices: [
                                    { value: -1, text: qsTr("No reminder") },
                                    { value: 0, text: qsTr("When it starts") },
                                    { value: 5, text: qsTr("5 minutes before") },
                                    { value: 15, text: qsTr("15 minutes before") },
                                    { value: 30, text: qsTr("30 minutes before") },
                                    { value: 60, text: qsTr("1 hour before") }
                                ]
                                model: choices
                                currentIndex: Math.max(0, choices.findIndex(c => c.value === prefs.defaultReminder))
                                onActivated: function (i) {
                                    prefs.defaultReminder = choices[i].value;
                                }
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Default length")
                            Row {
                                spacing: 6
                                UI.SegmentButton {
                                    text: qsTr("30 min")
                                    checked: prefs.defaultLength === 30
                                    onClicked: prefs.defaultLength = 30
                                }
                                UI.SegmentButton {
                                    text: qsTr("1 hour")
                                    checked: prefs.defaultLength === 60
                                    onClicked: prefs.defaultLength = 60
                                }
                            }
                        }
                    }
                }

                // Calendar: extra calendars
                UI.Label {
                    visible: page.section === "calendar"
                    Layout.topMargin: 4
                    text: qsTr("Extra calendars")
                    size: 13
                    font.weight: UI.Style.weightSemiBold
                }
                UI.Card {
                    visible: page.section === "calendar"
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("UK bank holidays")
                            description: qsTr("England and Wales, from GOV.UK")
                            UI.Toggle {
                                checked: CalendarEngine.calendars.length >= 0 && CalendarEngine.hasHolidays()
                                onToggled: CalendarEngine.setHolidays(checked)
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Birthdays from contacts")
                            description: qsTr("From the birthdays saved with your contacts")
                            UI.Toggle {
                                checked: CalendarEngine.calendars.length >= 0 && CalendarEngine.hasBirthdays()
                                onToggled: CalendarEngine.setBirthdays(checked)
                            }
                        }
                    }
                }
                Item {
                    implicitHeight: 20
                }
            }
        }
    }

    UI.Dialog {
        id: removeAccount
        property var account: ({})
        function ask(a) {
            account = a;
            open();
        }
        title: qsTr("Remove %1?").arg(account.name)
        text: qsTr("Its calendars leave this PC. Nothing is deleted from the account itself.")
        iconName: "warning"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: removeAccount.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Remove")
                onClicked: {
                    CalendarEngine.removeAccount(removeAccount.account.uid);
                    removeAccount.close();
                }
            }
        ]
    }
}
