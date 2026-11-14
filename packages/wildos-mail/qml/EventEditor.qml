import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// A new event or one being changed (CalMsNew.dc), in place of the days: the
// title, who's invited, when, how often, where, the reminder, the calendar
// and notes, with the chosen day beside the form so a clash shows before
// saving.
Rectangle {
    id: editor

    // The event being changed, from CalendarEngine.event(); empty for a new one.
    property var original: ({})
    property string calendarUid
    property date startTime: new Date()
    property date endTime: new Date()
    property bool allDay: false
    property bool busy: true
    property bool isPrivate: false
    property string repeat: "none"
    property int reminder: 15
    property var dayEvents: []
    property date shownDay: startTime

    signal closed()

    readonly property bool editing: original.key !== undefined
    readonly property bool repeatOfSeries: editing && original.recurring === true

    function start(event, defaults) {
        original = event || {};
        const now = new Date();
        if (editing) {
            title.text = original.title === qsTr("(No title)") ? "" : original.title;
            people.people = (original.people || []).filter(p => !p.me).map(p => p.name ? p.name + " <" + p.email + ">" : p.email);
            location.text = original.location || "";
            notes.text = original.description || "";
            calendarUid = original.calendarUid;
            allDay = original.allDay === true;
            startTime = new Date(original.start);
            endTime = allDay ? new Date(new Date(original.end).getTime() - 86400000) : new Date(original.end);
            busy = original.busy !== false;
            isPrivate = original.private === true;
            repeat = original.repeat || "none";
            reminder = original.reminder === undefined ? -1 : original.reminder;
        } else {
            title.text = "";
            people.setText((defaults && defaults.attendees) ? defaults.attendees.join(", ") : "");
            location.text = "";
            notes.text = "";
            calendarUid = CalendarEngine.defaultCalendar;
            allDay = defaults && defaults.allDay === true;
            const s = defaults && defaults.start ? new Date(defaults.start) : new Date(now.getFullYear(), now.getMonth(), now.getDate(), now.getHours() + 1);
            startTime = s;
            endTime = allDay ? s : new Date(s.getTime() + prefs.defaultLength * 60000);
            busy = true;
            isPrivate = false;
            repeat = "none";
            reminder = allDay ? -1 : prefs.defaultReminder;
        }
        shownDay = startTime;
        title.forceActiveFocus();
    }
    function save(wholeSeries) {
        const s = startTime;
        let e = endTime;
        if (!allDay && e <= s) {
            e = new Date(s.getTime() + 30 * 60000);
        }
        CalendarEngine.saveEvent({
            key: original.key || "",
            calendarUid: calendarUid,
            title: title.text.trim().length > 0 ? title.text.trim() : qsTr("(No title)"),
            start: s,
            end: e,
            allDay: allDay,
            location: location.text,
            description: notes.text,
            reminder: reminder,
            repeat: repeat,
            attendees: people.people.concat(people.text.length > 0 && people.people.length === 0 ? [people.text] : []).map(a => people.addressOf(a)),
            busy: busy,
            private: isPrivate,
            wholeSeries: wholeSeries
        });
        closed();
    }
    function withDate(day, time) {
        return new Date(day.getFullYear(), day.getMonth(), day.getDate(), time.getHours(), time.getMinutes());
    }
    function timeChoices(from) {
        const list = [];
        for (let m = 0; m < 24 * 60; m += 15) {
            list.push({ value: m, text: Formats.time(new Date(2000, 0, 1, Math.floor(m / 60), m % 60)) });
        }
        return list;
    }
    readonly property var times: timeChoices()
    function minutesOf(d) {
        return d.getHours() * 60 + d.getMinutes();
    }
    function indexFor(d) {
        const m = minutesOf(d);
        return Math.min(times.length - 1, Math.round(m / 15));
    }

    onShownDayChanged: CalendarEngine.loadDay(shownDay)
    Connections {
        target: CalendarEngine
        function onDayLoaded(day, events) {
            if (day.getFullYear() === editor.shownDay.getFullYear() && day.getMonth() === editor.shownDay.getMonth() && day.getDate() === editor.shownDay.getDate()) {
                editor.dayEvents = events.filter(e => e.key !== editor.original.key);
            }
        }
    }

    radius: 10
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle
    clip: true

    Settings {
        id: prefs
        category: "calendar"
        property int defaultLength: 60
        property int defaultReminder: 15
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Save, Discard, Busy, Repeat, Private.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 6
            UI.Button {
                id: saveButton
                kind: "primary"
                size: "small"
                text: qsTr("Save")
                iconName: "check"
                onClicked: editor.repeatOfSeries ? seriesMenu.popup(saveButton, 0, saveButton.height + 4) : editor.save(true)
            }
            UI.Button {
                size: "small"
                text: qsTr("Discard")
                iconName: "trash"
                onClicked: editor.closed()
            }
            Rectangle {
                implicitWidth: 1
                implicitHeight: 22
                Layout.leftMargin: 6
                Layout.rightMargin: 6
                color: UI.Style.borderSubtle
            }
            CommandButton {
                text: editor.busy ? qsTr("Busy") : qsTr("Free")
                iconName: "clock"
                tip: qsTr("Whether this time shows as busy to people who can see your calendar")
                onClicked: editor.busy = !editor.busy
            }
            CommandButton {
                id: repeatButton
                text: qsTr("Repeat")
                iconName: "repeat"
                onClicked: repeatMenu.popup(repeatButton, 0, repeatButton.height + 4)
            }
            CommandButton {
                text: editor.isPrivate ? qsTr("Private") : qsTr("Not private")
                iconName: "lock"
                tip: qsTr("Private events show only as busy to people you share this calendar with")
                onClicked: editor.isPrivate = !editor.isPrivate
            }
            Item {
                Layout.fillWidth: true
            }
            UI.IconButton {
                iconName: "close"
                tip: qsTr("Close")
                onClicked: editor.closed()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: UI.Style.borderSubtle
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // The form.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 24
                spacing: 14

                QQC.TextField {
                    id: title
                    Layout.fillWidth: true
                    placeholderText: qsTr("Add a title")
                    placeholderTextColor: UI.Style.inkPlaceholder
                    color: UI.Style.ink
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(UI.Style.px(20))
                    font.pixelSize: UI.Style.px(20)
                    font.weight: UI.Style.weightSemiBold
                    leftPadding: 0
                    bottomPadding: 10
                    background: Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: title.activeFocus ? 2 : 1
                        color: title.activeFocus ? UI.Style.accent : UI.Style.borderSubtle
                    }
                    Keys.onReturnPressed: editor.save(true)
                    Keys.onPressed: event => {
                        if (titleMenu.isMenuKey(event)) {
                            titleMenu.openAtCaret();
                            event.accepted = true;
                        }
                    }

                    UI.TextMenu {
                        id: titleMenu
                    }
                }

                component FormRow: RowLayout {
                    id: formRow
                    property string icon
                    default property alias content: holder.data
                    Layout.fillWidth: true
                    spacing: 14
                    MailIcon {
                        Layout.alignment: Qt.AlignVCenter
                        name: formRow.icon
                        size: 17
                        color: UI.Style.iconMuted
                    }
                    RowLayout {
                        id: holder
                        Layout.fillWidth: true
                        spacing: 10
                    }
                }

                FormRow {
                    icon: "people"
                    RecipientField {
                        id: people
                        Layout.fillWidth: true
                        label: ""
                        placeholder: qsTr("Invite people")
                    }
                }
                // A calendar on this PC has nobody to send invitations as.
                UI.Label {
                    readonly property var calendar: CalendarEngine.calendars.find(c => c.uid === editor.calendarUid) || {}
                    visible: (people.people.length > 0 || people.text.length > 0) && !calendar.email
                    Layout.fillWidth: true
                    Layout.leftMargin: 31
                    Layout.topMargin: -8
                    text: qsTr("Only Google and CalDAV calendars send invitations. On this calendar the people are noted on the event but not told.")
                    size: 12
                    color: UI.Style.inkMuted
                    wrapMode: Text.WordWrap
                }

                FormRow {
                    icon: "clock"
                    QQC.Button {
                        id: dateButton
                        implicitWidth: 180
                        implicitHeight: 36
                        onClicked: datePopup.open()
                        background: Rectangle {
                            radius: 6
                            color: dateButton.hovered ? UI.Style.surfaceHover : UI.Style.surfaceControl
                            border.width: 1
                            border.color: UI.Style.borderField
                        }
                        contentItem: RowLayout {
                            UI.Label {
                                Layout.fillWidth: true
                                leftPadding: 4
                                text: Formats.dayAndShortDate(editor.startTime)
                                size: 13
                            }
                            MailIcon {
                                name: "chevron-down"
                                size: 12
                                color: UI.Style.iconMuted
                            }
                        }
                        QQC.Popup {
                            id: datePopup
                            y: parent.height + 4
                            width: 260
                            padding: 12
                            background: Rectangle {
                                radius: 10
                                color: UI.Style.glassMenu
                                border.width: 1
                                border.color: UI.Style.borderContextMenu
                            }
                            contentItem: MiniMonth {
                                month: new Date(editor.startTime.getFullYear(), editor.startTime.getMonth(), 1)
                                rangeFrom: new Date(editor.startTime.getFullYear(), editor.startTime.getMonth(), editor.startTime.getDate())
                                rangeTo: new Date(editor.startTime.getFullYear(), editor.startTime.getMonth(), editor.startTime.getDate() + 1)
                                onPicked: function (day) {
                                    const length = editor.endTime - editor.startTime;
                                    editor.startTime = editor.withDate(day, editor.startTime);
                                    editor.endTime = new Date(editor.startTime.getTime() + length);
                                    editor.shownDay = day;
                                    datePopup.close();
                                }
                            }
                        }
                    }
                    UI.ComboBox {
                        visible: !editor.allDay
                        implicitWidth: Formats.twelveHour() ? 116 : 96
                        model: editor.times
                        currentIndex: editor.indexFor(editor.startTime)
                        onActivated: function (i) {
                            const length = editor.endTime - editor.startTime;
                            const m = editor.times[i].value;
                            const s = editor.startTime;
                            editor.startTime = new Date(s.getFullYear(), s.getMonth(), s.getDate(), Math.floor(m / 60), m % 60);
                            editor.endTime = new Date(editor.startTime.getTime() + length);
                        }
                    }
                    UI.Label {
                        visible: !editor.allDay
                        text: qsTr("to")
                        size: 13
                        color: UI.Style.inkMuted
                    }
                    UI.ComboBox {
                        visible: !editor.allDay
                        implicitWidth: Formats.twelveHour() ? 116 : 96
                        model: editor.times
                        currentIndex: editor.indexFor(editor.endTime)
                        onActivated: function (i) {
                            const m = editor.times[i].value;
                            const s = editor.startTime;
                            let e = new Date(s.getFullYear(), s.getMonth(), s.getDate(), Math.floor(m / 60), m % 60);
                            // Past midnight means the next day.
                            if (e <= s) {
                                e = new Date(e.getTime() + 86400000);
                            }
                            editor.endTime = e;
                        }
                    }
                    UI.Toggle {
                        Layout.leftMargin: 8
                        checked: editor.allDay
                        onToggled: {
                            editor.allDay = checked;
                            if (!checked && editor.endTime <= editor.startTime) {
                                const s = editor.startTime;
                                editor.startTime = new Date(s.getFullYear(), s.getMonth(), s.getDate(), 9, 0);
                                editor.endTime = new Date(editor.startTime.getTime() + prefs.defaultLength * 60000);
                            }
                        }
                    }
                    UI.Label {
                        text: qsTr("All day")
                        size: 13
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                FormRow {
                    icon: "repeat"
                    UI.ComboBox {
                        id: repeatBox
                        implicitWidth: 220
                        readonly property var choices: [
                            { value: "none", text: qsTr("Doesn't repeat") },
                            { value: "daily", text: qsTr("Every day") },
                            { value: "weekdays", text: qsTr("Every weekday") },
                            { value: "weekly", text: qsTr("Every week on %1").arg(Formats.names().toString(editor.startTime, "dddd")) },
                            { value: "monthly", text: qsTr("Every month on the %1").arg(editor.startTime.getDate()) },
                            { value: "yearly", text: qsTr("Every year on %1").arg(Formats.dayAndMonth(editor.startTime)) }
                        ].concat(editor.repeat === "other" ? [{ value: "other", text: qsTr("As set in another app") }] : [])
                        model: choices
                        currentIndex: Math.max(0, choices.findIndex(c => c.value === editor.repeat))
                        onActivated: function (i) {
                            editor.repeat = choices[i].value;
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                FormRow {
                    icon: "location"
                    UI.TextField {
                        id: location
                        Layout.fillWidth: true
                        placeholderText: qsTr("Add a place")
                    }
                }

                FormRow {
                    icon: "bell"
                    UI.ComboBox {
                        implicitWidth: 200
                        readonly property var choices: [
                            { value: -1, text: qsTr("No reminder") },
                            { value: 0, text: qsTr("When it starts") },
                            { value: 5, text: qsTr("5 minutes before") },
                            { value: 15, text: qsTr("15 minutes before") },
                            { value: 30, text: qsTr("30 minutes before") },
                            { value: 60, text: qsTr("1 hour before") },
                            { value: 120, text: qsTr("2 hours before") },
                            { value: 1440, text: qsTr("1 day before") },
                            { value: 10080, text: qsTr("1 week before") }
                        ]
                        model: choices
                        currentIndex: Math.max(0, choices.findIndex(c => c.value === editor.reminder))
                        onActivated: function (i) {
                            editor.reminder = choices[i].value;
                        }
                    }
                    UI.ComboBox {
                        id: calendarBox
                        implicitWidth: 220
                        readonly property var choices: CalendarEngine.calendars.filter(c => !c.readOnly).map(c => ({ value: c.uid, text: c.name + (c.groupUid ? "  ·  " + c.group : ""), color: c.color }))
                        model: choices
                        currentIndex: Math.max(0, choices.findIndex(c => c.value === editor.calendarUid))
                        onActivated: function (i) {
                            editor.calendarUid = choices[i].value;
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                FormRow {
                    icon: "notes"
                    Layout.fillHeight: true
                    QQC.ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 90
                        QQC.TextArea {
                            id: notes
                            wrapMode: TextEdit.Wrap
                            color: UI.Style.ink
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
                            font.pixelSize: UI.Style.px(13)
                            placeholderText: qsTr("Add notes")
                            placeholderTextColor: UI.Style.inkPlaceholder
                            padding: 12
                            background: Rectangle {
                                radius: 6
                                color: UI.Style.surfaceSunken
                                border.width: 1
                                border.color: notes.activeFocus ? UI.Style.accent : UI.Style.borderField
                            }
                            Keys.onPressed: event => {
                                if (notesMenu.isMenuKey(event)) {
                                    notesMenu.openAtCaret();
                                    event.accepted = true;
                                }
                            }

                            UI.TextMenu {
                                id: notesMenu
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 1
                color: UI.Style.borderSubtle
            }

            // The day, with this event drawn among the others.
            ColumnLayout {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                Layout.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    UI.IconButton {
                        iconName: "chevron-left"
                        tip: qsTr("Previous day")
                        onClicked: editor.shownDay = new Date(editor.shownDay.getFullYear(), editor.shownDay.getMonth(), editor.shownDay.getDate() - 1)
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: Formats.weekdayAndDay(editor.shownDay)
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.IconButton {
                        iconName: "chevron-right"
                        tip: qsTr("Next day")
                        onClicked: editor.shownDay = new Date(editor.shownDay.getFullYear(), editor.shownDay.getMonth(), editor.shownDay.getDate() + 1)
                    }
                }

                // A clash, or none.
                Rectangle {
                    id: clashBar
                    readonly property bool sameDay: editor.shownDay.toDateString() === editor.startTime.toDateString()
                    readonly property var clashes: {
                        if (!sameDay || editor.allDay) {
                            return [];
                        }
                        const s = editor.minutesOf(editor.startTime);
                        const e = s + (editor.endTime - editor.startTime) / 60000;
                        return editor.dayEvents.filter(x => !x.allDay && x.busy && x.startMinute < e && x.endMinute > s);
                    }
                    visible: sameDay && !editor.allDay
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: 6
                    color: clashes.length > 0 ? UI.Style.warningSoft : UI.Style.successSoft
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        MailIcon {
                            name: clashBar.clashes.length > 0 ? "clock" : "check"
                            size: 14
                            color: clashBar.clashes.length > 0 ? UI.Style.warning : UI.Style.success
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: clashBar.clashes.length > 0 ? qsTr("Overlaps %1").arg(clashBar.clashes[0].title) : qsTr("You're free at this time")
                            size: 12
                            elide: Text.ElideRight
                        }
                    }
                }

                Flickable {
                    id: dayHours
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    readonly property real hour: 50
                    contentHeight: 24 * hour
                    contentY: Math.max(0, Math.min(contentHeight - height, (editor.allDay ? 8 : editor.startTime.getHours() - 1) * hour))
                    boundsBehavior: Flickable.StopAtBounds

                    Repeater {
                        model: 24
                        delegate: Item {
                            required property int index
                            y: index * dayHours.hour
                            width: dayHours.width
                            height: dayHours.hour
                            UI.Label {
                                width: 40
                                y: -6
                                visible: index > 0
                                text: Formats.hour(index)
                                size: 11
                                color: UI.Style.inkSubtle
                            }
                            Rectangle {
                                x: 46
                                width: parent.width - 46
                                height: 1
                                color: UI.Style.borderSubtle
                            }
                        }
                    }
                    Repeater {
                        model: editor.dayEvents.filter(e => !e.allDay)
                        delegate: Rectangle {
                            required property var modelData
                            x: 50
                            y: modelData.startMinute / 60 * dayHours.hour + 1
                            width: dayHours.width - 54
                            height: Math.max(18, (modelData.endMinute - modelData.startMinute) / 60 * dayHours.hour - 3)
                            radius: 4
                            color: Qt.rgba(Qt.color(modelData.color).r, Qt.color(modelData.color).g, Qt.color(modelData.color).b, 0.36)
                            clip: true
                            Rectangle {
                                width: 3
                                height: parent.height
                                color: modelData.color
                            }
                            Column {
                                x: 9
                                y: 3
                                UI.Label {
                                    text: modelData.title
                                    size: 12
                                    font.weight: UI.Style.weightSemiBold
                                }
                                UI.Label {
                                    text: modelData.rangeText
                                    size: 11
                                    color: UI.Style.inkMuted
                                }
                            }
                        }
                    }
                    // This event, dashed.
                    Rectangle {
                        visible: clashBar.sameDay && !editor.allDay
                        x: 50
                        y: editor.minutesOf(editor.startTime) / 60 * dayHours.hour + 1
                        width: dayHours.width - 54
                        height: Math.max(18, (editor.endTime - editor.startTime) / 3600000 * dayHours.hour - 3)
                        radius: 4
                        color: UI.Style.accentSoft
                        border.width: 1.5
                        border.color: UI.Style.accent
                        Column {
                            x: 9
                            y: 3
                            UI.Label {
                                text: title.text.length > 0 ? title.text : qsTr("New event")
                                size: 12
                                font.weight: UI.Style.weightSemiBold
                            }
                            UI.Label {
                                text: qsTr("%1 to %2").arg(Formats.time(editor.startTime)).arg(Formats.time(editor.endTime))
                                size: 11
                                color: UI.Style.inkMuted
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Menu {
        id: repeatMenu
        entries: repeatBox.choices.map(c => ({ text: c.text, checked: c.value === editor.repeat, run: () => { editor.repeat = c.value; } }))
    }
    UI.Menu {
        id: seriesMenu
        entries: [
            { text: qsTr("Save this one only"), run: () => editor.save(false) },
            { text: qsTr("Save the whole series"), run: () => editor.save(true) }
        ]
    }

    Shortcut {
        sequence: "Ctrl+S"
        enabled: editor.visible
        onActivated: editor.save(true)
    }
    Shortcut {
        sequence: "Escape"
        enabled: editor.visible
        onActivated: editor.closed()
    }
}
