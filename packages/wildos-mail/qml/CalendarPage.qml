import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// Calendar (CalendarMicrosoft.dc and the boards after it): the ribbon,
// then the small month and the calendars beside the day, week or month,
// which turns into the event being written. A tab of the Mail window, or
// a window of its own (CalendarMain) from "Open in new window"; the
// window around it has the title bar, its search and the app rail.
Item {
    id: window

    // Mail's Google accounts whose calendars aren't here yet, offered in a
    // bar (empty in a window of its own, which doesn't run Mail).
    property var mailAccounts: []
    signal addGoogleCalendar(string email)

    property string view: prefs.view
    // A day inside what's on screen.
    property date anchor: today()
    property bool editing: false
    property bool showSettings: false
    property string openKey: ""
    // An event to open once its days have loaded (from a reminder or search).
    property string pendingKey: ""

    function today() {
        const n = new Date();
        return new Date(n.getFullYear(), n.getMonth(), n.getDate());
    }
    function addDays(d, n) {
        return new Date(d.getFullYear(), d.getMonth(), d.getDate() + n);
    }
    function weekStart(d) {
        return addDays(d, -((d.getDay() - prefs.firstDay + 7) % 7));
    }
    readonly property var workDays: (prefs.workDays || "1,2,3,4,5").split(",").map(Number)
    // The work week runs from the first working day to the last, counted
    // from the first day of the week.
    readonly property int workFirst: Math.min.apply(null, workDays.map(d => (d - prefs.firstDay + 7) % 7))
    readonly property int workLast: Math.max.apply(null, workDays.map(d => (d - prefs.firstDay + 7) % 7))
    readonly property date rangeFrom: view === "day" ? anchor : view === "workweek" ? addDays(weekStart(anchor), workFirst)
        : view === "week" ? weekStart(anchor) : monthView.firstShown
    readonly property int rangeDays: view === "day" ? 1 : view === "workweek" ? workLast - workFirst + 1 : view === "week" ? 7 : 42
    readonly property date rangeTo: addDays(rangeFrom, rangeDays)
    onRangeFromChanged: Qt.callLater(pushRange)
    onRangeDaysChanged: Qt.callLater(pushRange)
    function pushRange() {
        CalendarEngine.setRange(rangeFrom, rangeTo);
    }

    readonly property string rangeTitle: {
        if (view === "day") {
            return Formats.longDate(anchor);
        }
        if (view === "month") {
            return Formats.monthAndYear(anchor);
        }
        const last = addDays(rangeTo, -1);
        // "9 to 15 November 2026" only reads right where the day comes first.
        if (rangeFrom.getMonth() === last.getMonth() && Formats.dayFirst()) {
            return qsTr("%1 to %2").arg(rangeFrom.getDate()).arg(Formats.dayMonthYear(last));
        }
        if (rangeFrom.getFullYear() === last.getFullYear()) {
            return qsTr("%1 to %2").arg(Formats.dayAndMonth(rangeFrom)).arg(Formats.dayMonthYear(last));
        }
        return qsTr("%1 to %2").arg(Formats.dayMonthYear(rangeFrom)).arg(Formats.dayMonthYear(last));
    }

    function step(n) {
        if (view === "month") {
            anchor = new Date(anchor.getFullYear(), anchor.getMonth() + n, 1);
        } else {
            anchor = addDays(anchor, n * (view === "day" ? 1 : 7));
        }
        miniMonth.month = new Date(anchor.getFullYear(), anchor.getMonth(), 1);
    }
    function goTo(day) {
        anchor = new Date(day.getFullYear(), day.getMonth(), day.getDate());
        miniMonth.month = new Date(anchor.getFullYear(), anchor.getMonth(), 1);
    }
    function setView(v) {
        view = v;
        prefs.view = v;
        editing = false;
        showSettings = false;
    }
    function newEvent(defaults) {
        if (!CalendarEngine.ready) {
            return;
        }
        showSettings = false;
        peek.close();
        editing = true;
        editor.start(null, defaults || {});
    }
    function editEvent(event) {
        showSettings = false;
        editing = true;
        editor.start(event, {});
    }
    function openEvent(key, block) {
        openKey = key;
        peek.show(key, block);
    }
    // An event's right-click menu: at the pointer, or under the event when
    // it was asked for with the keyboard.
    function showEventMenu(key, from) {
        const e = CalendarEngine.event(key);
        if (!e.key) {
            return;
        }
        peek.close();
        eventMenu.event = e;
        if (from) {
            eventMenu.popup(from, 0, from.height + 4);
        } else {
            eventMenu.popup();
        }
    }
    // An empty slot's. A whole day (the all-day band, a day in Month) starts
    // its timed event when the working day does.
    function showSlotMenu(start, allDay) {
        slotMenu.day = new Date(start.getFullYear(), start.getMonth(), start.getDate());
        slotMenu.start = allDay ? new Date(start.getFullYear(), start.getMonth(), start.getDate(), Math.floor(prefs.workStart), 0) : start;
        slotMenu.popup();
    }
    // From the command line: a reminder's Open, or search.
    function showEvent(key, day) {
        editing = false;
        showSettings = false;
        if (day) {
            goTo(day);
        }
        pendingKey = key;
        Qt.callLater(tryPending);
    }
    property string lastSearch
    function search(text) {
        lastSearch = text;
        if (text.trim().length > 0) {
            CalendarEngine.search(text);
        } else {
            results.close();
        }
    }
    function clearSearch() {
        lastSearch = "";
        results.close();
    }
    function toggleSettings() {
        editing = false;
        showSettings = !showSettings;
    }
    // Where People and Tasks send you to add an account: the same
    // accounts bring contacts and task lists with them.
    function showAccounts() {
        editing = false;
        settingsPage.section = "accounts";
        showSettings = true;
    }
    function tryPending() {
        if (!pendingKey) {
            return;
        }
        const e = CalendarEngine.event(pendingKey.split("\u001f").length === 3 ? pendingKey : pendingKey + "\u001f");
        if (e.key) {
            pendingKey = "";
            openKey = e.key;
            peek.show(e.key, header);
        }
    }

    Settings {
        id: prefs
        category: "calendar"
        property string view: "week"
        property int firstDay: 1
        property string workDays: "1,2,3,4,5"
        property real workStart: 9
        property real workEnd: 17.5
        property int defaultReminder: 15
        property int defaultLength: 60
        property bool weekNumbers: false
    }

    Connections {
        target: CalendarEngine
        function onNotice(text) {
            toast.show(text);
        }
        function onEventsChanged() {
            if (window.pendingKey) {
                window.tryPending();
            }
        }
        function onSaved(key) {
            toast.show(qsTr("Saved"));
        }
    }

    Shortcut {
        sequence: StandardKey.New
        enabled: window.visible
        onActivated: window.newEvent({})
    }
    Shortcut {
        sequence: "F5"
        enabled: window.visible
        onActivated: CalendarEngine.refresh()
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: window.visible && !window.editing
        onActivated: window.step(-1)
    }
    Shortcut {
        sequence: "Alt+Right"
        enabled: window.visible && !window.editing
        onActivated: window.step(1)
    }
    Shortcut {
        sequence: "Ctrl+1"
        enabled: window.visible
        onActivated: window.setView("day")
    }
    Shortcut {
        sequence: "Ctrl+2"
        enabled: window.visible
        onActivated: window.setView("workweek")
    }
    Shortcut {
        sequence: "Ctrl+3"
        enabled: window.visible
        onActivated: window.setView("week")
    }
    Shortcut {
        sequence: "Ctrl+4"
        enabled: window.visible
        onActivated: window.setView("month")
    }
    Shortcut {
        sequence: "Ctrl+T"
        enabled: window.visible
        onActivated: window.goTo(window.today())
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The calendar service didn't start.
        Rectangle {
            visible: CalendarEngine.problem.length > 0
            Layout.fillWidth: true
            implicitHeight: 40
            color: UI.Style.warningSoft
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: CalendarEngine.problem
                size: 13
            }
        }

        // A Google account Mail has whose calendar isn't here yet.
        Repeater {
            model: window.mailAccounts.filter(a => !CalendarEngine.accounts.some(c => (c.email || "").toLowerCase() === a.email.toLowerCase()))
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 12
                Layout.topMargin: 8
                implicitHeight: 44
                radius: UI.Style.radiusSm
                color: UI.Style.surfaceRaised
                border.width: 1
                border.color: UI.Style.borderSubtle
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 10
                    MailIcon {
                        name: "calendar"
                        size: 16
                        color: UI.Style.iconInk
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: modelData.auth === "microsoft" ? qsTr("Show the calendar for %1 here too.").arg(modelData.email)
                                                             : qsTr("Show the calendar for %1 here too. Google asks once to allow it.").arg(modelData.email)
                        size: 13
                        elide: Text.ElideRight
                    }
                    UI.Button {
                        size: "small"
                        kind: "primary"
                        text: GoogleSignIn.waiting ? qsTr("Waiting for Google") : qsTr("Show my calendar")
                        busy: GoogleSignIn.waiting
                        enabled: !GoogleSignIn.waiting
                        onClicked: window.addGoogleCalendar(modelData.email)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.bottomMargin: 12
                spacing: 10

                // The ribbon: Home, View and Help.
                ColumnLayout {
                    visible: !window.showSettings
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 12
                    spacing: 2

                    Row {
                        id: tabs
                        property string current: "home"
                        leftPadding: 4
                        spacing: 22
                        Repeater {
                            model: [{ id: "home", name: qsTr("Home") }, { id: "view", name: qsTr("View") }, { id: "help", name: qsTr("Help") }]
                            delegate: Item {
                                id: tab
                                required property var modelData
                                readonly property bool active: tabs.current === modelData.id
                                width: tabLabel.implicitWidth + 4
                                height: 36
                                UI.Label {
                                    id: tabLabel
                                    anchors.centerIn: parent
                                    text: tab.modelData.name
                                    size: 13
                                    font.weight: tab.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                                    color: tab.active ? UI.Style.ink : UI.Style.inkMuted
                                }
                                Rectangle {
                                    visible: tab.active
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 4
                                    height: 2
                                    radius: 1
                                    color: UI.Style.accent
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: tabs.current = tab.modelData.id
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 46
                        radius: UI.Style.radiusPanel
                        color: UI.Style.surfaceRest
                        border.width: 1
                        border.color: UI.Style.borderSubtle
                        clip: true

                        // Home
                        RowLayout {
                            visible: tabs.current === "home"
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 2

                            UI.Button {
                                kind: "primary"
                                size: "small"
                                text: qsTr("New event")
                                iconName: "plus"
                                enabled: CalendarEngine.ready
                                Layout.rightMargin: 6
                                onClicked: window.newEvent({})
                            }
                            Repeater {
                                model: [
                                    { id: "day", name: qsTr("Day"), icon: "calendar" },
                                    { id: "workweek", name: qsTr("Work week"), icon: "calendar" },
                                    { id: "week", name: qsTr("Week"), icon: "calendar" },
                                    { id: "month", name: qsTr("Month"), icon: "grid" }
                                ]
                                delegate: CommandButton {
                                    required property var modelData
                                    text: modelData.name
                                    iconName: modelData.icon
                                    checkable: false
                                    background: Rectangle {
                                        id: viewBackground
                                        radius: UI.Style.radiusXs
                                        // Fades in on hover and clears at once, so moving along a list
                                        // never lights two items. It comes before the colour so it sees
                                        // the change in hover first.
                                        Behavior on color {
                                            enabled: viewBackground.parent.hovered
                                            ColorAnimation { duration: UI.Style.hoverDuration }
                                        }
                                        color: window.view === modelData.id && !window.editing ? UI.Style.surfaceSelected : parent.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
                                    }
                                    onClicked: window.setView(modelData.id)
                                }
                            }
                            Rectangle {
                                implicitWidth: 1
                                implicitHeight: 22
                                Layout.leftMargin: 6
                                Layout.rightMargin: 6
                                color: UI.Style.borderSubtle
                            }
                            CommandButton {
                                text: qsTr("Today")
                                iconName: "calendar"
                                onClicked: {
                                    window.editing = false;
                                    window.goTo(window.today());
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "refresh"
                                tip: qsTr("Check for changes")
                                spinning: CalendarEngine.syncing
                                onClicked: CalendarEngine.refresh()
                            }
                        }

                        // View
                        RowLayout {
                            visible: tabs.current === "view"
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 2
                            CommandButton {
                                text: prefs.firstDay === 1 ? qsTr("Weeks start on Monday") : qsTr("Weeks start on Sunday")
                                iconName: "calendar"
                                onClicked: prefs.firstDay = prefs.firstDay === 1 ? 0 : 1
                            }
                            CommandButton {
                                text: qsTr("Calendar settings")
                                iconName: "settings"
                                onClicked: {
                                    settingsPage.section = "calendar";
                                    window.showSettings = true;
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        // Help
                        RowLayout {
                            visible: tabs.current === "help"
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 2
                            CommandButton {
                                text: qsTr("Using Calendar")
                                iconName: "external"
                                onClicked: Qt.openUrlExternally("https://help.wildos.io/using/calendar/")
                            }
                            CommandButton {
                                text: qsTr("Keyboard shortcuts")
                                iconName: "external"
                                onClicked: Qt.openUrlExternally("https://help.wildos.io/using/calendar/#keyboard-shortcuts")
                            }
                            CommandButton {
                                text: qsTr("Report a problem")
                                iconName: "warning"
                                onClicked: Qt.openUrlExternally("wildos-report:calendar")
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.rightMargin: 12
                    spacing: 12
                    visible: !window.showSettings

                    // The small month and the calendars.
                    ColumnLayout {
                        Layout.preferredWidth: 236
                        Layout.maximumWidth: 236
                        Layout.fillWidth: false
                        Layout.fillHeight: true
                        Layout.leftMargin: 14
                        spacing: 14

                        MiniMonth {
                            id: miniMonth
                            Layout.fillWidth: true
                            firstDay: prefs.firstDay
                            // In Month, the week of the day chosen, as the board has it.
                            rangeFrom: window.view === "month" ? window.weekStart(window.anchor) : window.rangeFrom
                            rangeTo: window.view === "month" ? window.addDays(window.weekStart(window.anchor), 7) : window.rangeTo
                            onPicked: function (day) {
                                window.editing = false;
                                window.goTo(day);
                            }
                        }

                        CalendarList {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            onAddCalendar: addCalendar.start()
                        }
                    }

                    // The days, or the event being written.
                    Rectangle {
                        id: main
                        visible: !window.editing
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: UI.Style.radiusPanel
                        color: UI.Style.surfaceRest
                        border.width: 1
                        border.color: UI.Style.borderSubtle
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            RowLayout {
                                id: header
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 14
                                Layout.topMargin: 10
                                Layout.bottomMargin: 6
                                spacing: 8
                                UI.Button {
                                    size: "small"
                                    text: qsTr("Today")
                                    onClicked: window.goTo(window.today())
                                }
                                UI.IconButton {
                                    iconName: "chevron-left"
                                    tip: window.view === "month" ? qsTr("Previous month") : window.view === "day" ? qsTr("Previous day") : qsTr("Previous week")
                                    onClicked: window.step(-1)
                                }
                                UI.IconButton {
                                    iconName: "chevron-right"
                                    tip: window.view === "month" ? qsTr("Next month") : window.view === "day" ? qsTr("Next day") : qsTr("Next week")
                                    onClicked: window.step(1)
                                }
                                UI.Label {
                                    Layout.leftMargin: 8
                                    text: window.rangeTitle
                                    size: 17
                                    font.weight: UI.Style.weightSemiBold
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                UI.Label {
                                    text: qsTr("Times in %1").arg(CalendarEngine.timeZoneName)
                                    size: 12
                                    color: UI.Style.inkMuted
                                }
                            }

                            WeekView {
                                id: weekView
                                visible: window.view !== "month"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                startDate: window.view === "month" ? window.anchor : window.rangeFrom
                                days: window.view === "month" ? 7 : window.rangeDays
                                scrollToHour: Math.max(0, Math.floor(prefs.workStart) - 1)
                                openKey: peek.opened ? window.openKey : ""
                                onEventClicked: function (key, block) {
                                    window.openEvent(key, block);
                                }
                                onNewAt: function (start, allDay) {
                                    window.newEvent({ start: start, allDay: allDay });
                                }
                                onDayClicked: function (day) {
                                    window.goTo(day);
                                    window.setView("day");
                                }
                                onEventMenu: function (key, from) {
                                    window.showEventMenu(key, from);
                                }
                                onSlotMenu: function (start, allDay) {
                                    window.showSlotMenu(start, allDay);
                                }
                            }

                            MonthView {
                                id: monthView
                                visible: window.view === "month"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                month: window.anchor
                                firstDay: prefs.firstDay
                                openKey: peek.opened ? window.openKey : ""
                                onEventClicked: function (key, block) {
                                    window.openEvent(key, block);
                                }
                                onNewAt: function (start, allDay) {
                                    const d = start;
                                    window.newEvent({ start: new Date(d.getFullYear(), d.getMonth(), d.getDate(), Math.floor(prefs.workStart), 0), allDay: false });
                                }
                                onDayClicked: function (day) {
                                    window.goTo(day);
                                    window.setView("day");
                                }
                                onEventMenu: function (key, from) {
                                    window.showEventMenu(key, from);
                                }
                                onSlotMenu: function (start, allDay) {
                                    window.showSlotMenu(start, allDay);
                                }
                            }
                        }

                        // Before the calendar service has answered.
                        UI.Spinner {
                            anchors.centerIn: parent
                            visible: !CalendarEngine.ready && CalendarEngine.problem.length === 0
                            running: visible
                        }
                    }

                    EventEditor {
                        id: editor
                        visible: window.editing
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onClosed: window.editing = false
                    }
                }

                CalendarSettings {
                    id: settingsPage
                    visible: window.showSettings
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.rightMargin: 12
                    onCloseRequested: window.showSettings = false
                    onAddAccount: addCalendar.start()
                }
            }
        }
    }

    EventPeek {
        id: peek
        parent: QQC.Overlay.overlay
        onEditRequested: function (event) {
            window.editEvent(event);
        }
        onMessageEveryone: function (addresses, subject) {
            Qt.openUrlExternally("mailto:" + addresses.join(",") + "?subject=" + encodeURIComponent(subject));
        }
    }

    UI.Menu {
        id: eventMenu
        property var event: ({})
        entries: {
            const e = eventMenu.event;
            const can = e.key !== undefined && !e.readOnly;
            const remove = e.recurring ? [
                { text: qsTr("Delete this one"), enabled: can, run: () => CalendarEngine.removeEvent(e.key, false) },
                { text: qsTr("Delete the whole series"), icon: "delete", danger: true, enabled: can, run: () => CalendarEngine.removeEvent(e.key, true) }
            ] : [
                { text: qsTr("Delete"), icon: "delete", danger: true, enabled: can, run: () => CalendarEngine.removeEvent(e.key, true) }
            ];
            return [
                { text: qsTr("Edit"), icon: "pencil", enabled: can, run: () => window.editEvent(e) },
                { separator: true }
            ].concat(remove);
        }
    }
    UI.Menu {
        id: slotMenu
        property date day
        property date start
        entries: [
            { text: qsTr("New event"), icon: "plus", enabled: CalendarEngine.ready, run: () => window.newEvent({ start: slotMenu.start, allDay: false }) },
            { text: qsTr("New all-day event"), enabled: CalendarEngine.ready, run: () => window.newEvent({ start: slotMenu.day, allDay: true }) }
        ]
    }

    // Search results, under the search box.
    QQC.Popup {
        id: results
        property var items: []
        parent: QQC.Overlay.overlay
        x: (parent.width - width) / 2
        y: 44
        width: Math.min(520, parent.width - 160)
        height: Math.min(420, list.contentHeight + 16)
        padding: 8
        background: Rectangle {
            radius: 10
            color: UI.Style.glassMenu
            border.width: 1
            border.color: UI.Style.borderContextMenu
        }
        contentItem: ListView {
            id: list
            clip: true
            model: results.items
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 48
                radius: UI.Style.radiusXs
                color: rowMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                Rectangle {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4
                    height: 30
                    radius: 2
                    color: modelData.color
                }
                Column {
                    x: 24
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 34
                    UI.Label {
                        width: parent.width
                        text: modelData.title
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                        elide: Text.ElideRight
                    }
                    UI.Label {
                        width: parent.width
                        text: Formats.mediumDate(new Date(modelData.start)) + (modelData.allDay ? "" : ", " + Formats.time(new Date(modelData.start)))
                            + (modelData.location ? "  ·  " + modelData.location : "")
                        size: 12
                        color: UI.Style.inkMuted
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        results.close();
                        window.showEvent(modelData.key, new Date(modelData.start));
                    }
                }
            }
        }
        Connections {
            target: CalendarEngine
            function onSearchDone(text, items) {
                if (text !== window.lastSearch) {
                    return;
                }
                results.items = items.length > 0 ? items : [];
                if (items.length > 0) {
                    results.open();
                } else {
                    results.close();
                    toast.show(qsTr("Nothing found for \"%1\" in the year either side of today.").arg(text));
                }
            }
        }
    }

    AddCalendar {
        id: addCalendar
        parent: QQC.Overlay.overlay
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 60
    }
}
