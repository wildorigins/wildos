import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// Tasks (MailTasks.dc): the ribbon, then My day, Important, Planned, All
// tasks, Flagged email and each task list on the left, the tasks in the
// middle with the done ones folded under Completed, and the task chosen on
// the right with its steps, dates, file and note. A tab of the Mail
// window; the window around it has the title bar, its search and the rail.
Item {
    id: page

    signal openMessage(string key)
    signal manageAccounts()

    // myday, important, planned, all, flagged, or a list's uid.
    property string view: "myday"
    property string filterText: ""
    property string openKey: ""
    property var current: ({})
    // Flagged messages, as Mail has them: [{key, subject, from, date}].
    property var flagged: []
    // What the "Add a task" field's own buttons chose for the next task.
    property var pending: ({})

    readonly property bool smartView: ["myday", "important", "planned", "all", "flagged"].indexOf(view) >= 0
    readonly property var openRows: TasksEngine.revision >= 0 && view !== "flagged" ? TasksEngine.tasks(view, filterText, false, prefs.sort) : []
    readonly property var doneRows: TasksEngine.revision >= 0 && view !== "flagged" ? TasksEngine.tasks(view, filterText, true, prefs.sort) : []
    readonly property var flaggedRows: filterText ? flagged.filter(f => f.subject.toLowerCase().indexOf(filterText.toLowerCase()) >= 0) : flagged
    readonly property var currentList: TasksEngine.lists.find(l => l.uid === view) || ({})
    readonly property string viewTitle: {
        switch (view) {
        case "myday": return qsTr("My day");
        case "important": return qsTr("Important");
        case "planned": return qsTr("Planned");
        case "all": return qsTr("All tasks");
        case "flagged": return qsTr("Flagged email");
        }
        return currentList.name || "";
    }
    readonly property string viewIcon: {
        switch (view) {
        case "myday": return "sun";
        case "important": return "star";
        case "planned": return "calendar";
        case "all": return "tasks";
        case "flagged": return "flag";
        }
        return "folder";
    }

    function search(text) {
        filterText = text;
    }
    function clearSearch() {
        filterText = "";
    }
    function pick(v) {
        view = v;
        if (v === "flagged") {
            MailEngine.findFlagged();
        }
    }
    function open(key) {
        commitEditors();
        openKey = key;
        current = key ? TasksEngine.task(key) : ({});
        syncEditors(true);
    }
    function commitEditors() {
        if (!current.key) {
            return;
        }
        if (titleEdit.text.trim() && titleEdit.text.trim() !== current.rawTitle) {
            TasksEngine.setTitle(current.key, titleEdit.text);
        }
        if (notesEdit.text !== (current.notes || "")) {
            TasksEngine.setNotes(current.key, notesEdit.text);
        }
    }
    // The title and note are typed into in place, so a change arriving
    // from the service mustn't overwrite them mid-sentence.
    function syncEditors(force) {
        if (force || !titleEdit.activeFocus) {
            titleEdit.text = current.rawTitle || "";
        }
        if (force || !notesEdit.activeFocus) {
            notesEdit.text = current.notes || "";
        }
    }
    function today() {
        const n = new Date();
        return new Date(n.getFullYear(), n.getMonth(), n.getDate());
    }
    function addDays(d, n) {
        return new Date(d.getFullYear(), d.getMonth(), d.getDate() + n);
    }
    function nextMonday() {
        const t = today();
        return addDays(t, ((8 - t.getDay()) % 7) || 7);
    }
    function addTask() {
        const text = newTask.text.trim();
        if (!text) {
            return;
        }
        TasksEngine.addTask(text, view === "flagged" ? "all" : view, pending);
        newTask.text = "";
        pending = ({});
    }
    function repeatText(r) {
        switch (r) {
        case "daily": return qsTr("Every day");
        case "weekdays": return qsTr("Every weekday");
        case "weekly": return qsTr("Every week");
        case "monthly": return qsTr("Every month");
        case "yearly": return qsTr("Every year");
        case "other": return qsTr("Repeats");
        }
        return "";
    }
    function sizeText(bytes) {
        return bytes >= 1048576 ? qsTr("%1 MB").arg((bytes / 1048576).toFixed(1)) : qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)));
    }
    // Menus for a date, a reminder or a repeat; apply gets the choice.
    function dueEntries(apply, hasOne) {
        return [
            { text: qsTr("Today"), run: () => apply(today(), true) },
            { text: qsTr("Tomorrow"), run: () => apply(addDays(today(), 1), true) },
            { text: qsTr("Next week"), run: () => apply(nextMonday(), true) },
            { text: qsTr("Pick a date"), run: () => { duePicker.apply = apply; duePicker.ask(qsTr("Due date"), new Date(NaN), false); } },
            { separator: true, visible: hasOne },
            { text: qsTr("Remove due date"), icon: "delete", visible: hasOne, run: () => apply(new Date(NaN), true) }
        ].filter(e => e.visible !== false);
    }
    function reminderEntries(apply, hasOne) {
        const later = new Date();
        later.setHours(later.getHours() + 3, 0, 0, 0);
        const tomorrow = addDays(today(), 1);
        tomorrow.setHours(9, 0, 0, 0);
        const week = nextMonday();
        week.setHours(9, 0, 0, 0);
        return [
            { text: qsTr("Later today, %1").arg(Formats.time(later)), icon: "bell", visible: later.getDate() === new Date().getDate(), run: () => apply(later) },
            { text: qsTr("Tomorrow, %1").arg(Formats.time(tomorrow)), run: () => apply(tomorrow) },
            { text: qsTr("Next week, %1").arg(Formats.time(week)), run: () => apply(week) },
            { text: qsTr("Pick a date and time"), run: () => { reminderPicker.apply = apply; reminderPicker.ask(qsTr("Remind me"), new Date(NaN), true); } },
            { separator: true, visible: hasOne },
            { text: qsTr("Remove reminder"), icon: "delete", visible: hasOne, run: () => apply(new Date(NaN)) }
        ].filter(e => e.visible !== false);
    }
    function repeatEntries(apply, now) {
        return [
            { value: "daily", text: qsTr("Every day") },
            { value: "weekdays", text: qsTr("Every weekday") },
            { value: "weekly", text: qsTr("Every week") },
            { value: "monthly", text: qsTr("Every month") },
            { value: "yearly", text: qsTr("Every year") },
            { value: "none", text: qsTr("Don't repeat") }
        ].map(c => ({ text: c.text, radio: true, checked: (now || "none") === c.value, run: () => apply(c.value) }));
    }
    function popupMenu(entries, item) {
        dynamicMenu.entries = entries;
        if (item) {
            dynamicMenu.popup(item, 0, item.height + 4);
        } else {
            dynamicMenu.popup();
        }
    }
    function setDueFor(key, when, allDay) {
        TasksEngine.setDue(key, when, allDay);
    }

    Settings {
        id: prefs
        category: "tasks"
        property string sort: "created"
        property bool showCompleted: true
    }

    Connections {
        target: TasksEngine
        function onTasksChanged() {
            if (page.openKey) {
                const now = TasksEngine.task(page.openKey);
                if (!now.key) {
                    page.openKey = "";
                    page.current = ({});
                } else {
                    page.current = now;
                    page.syncEditors(false);
                }
            }
        }
        function onAdded(key) {
            page.open(key);
        }
        function onNotice(text) {
            toast.show(text);
        }
    }
    Connections {
        target: MailEngine
        function onFlaggedFound(messages) {
            page.flagged = messages;
        }
    }
    onVisibleChanged: if (visible) MailEngine.findFlagged()
    Component.onCompleted: MailEngine.findFlagged()

    Shortcut {
        sequence: StandardKey.New
        enabled: page.visible
        onActivated: newTask.forceActiveFocus()
    }

    function rowPicked(key, fromMail, menu) {
        if (fromMail) {
            page.openMessage(key);
            return;
        }
        page.open(key);
        if (menu) {
            page.popupMenu(taskMenuEntries(), null);
        }
    }
    function rowTicked(row, fromMail) {
        if (fromMail) {
            MailEngine.setFlaggedKeys([row.key], false);
            page.flagged = page.flagged.filter(f => f.key !== row.key);
        } else {
            TasksEngine.setDone(row.key, !row.done);
        }
    }
    function taskMenuEntries() {
        const t = current;
        if (!t.key) {
            return [];
        }
        return [
            { text: t.done ? qsTr("Mark as not done") : qsTr("Mark complete"), icon: "check", run: () => TasksEngine.setDone(t.key, !t.done) },
            { text: t.important ? qsTr("Not important") : qsTr("Important"), icon: "star", run: () => TasksEngine.setImportant(t.key, !t.important) },
            { text: t.myDay ? qsTr("Remove from My day") : qsTr("Add to My day"), icon: "sun", run: () => TasksEngine.setMyDay(t.key, !t.myDay) },
            { separator: true },
            { text: qsTr("Due date"), submenu: dueEntries((d, a) => page.setDueFor(t.key, d, a), !!t.dueText) },
            { text: qsTr("Remind me"), submenu: reminderEntries(d => TasksEngine.setReminder(t.key, d), t.reminder) },
            { text: qsTr("Move to list"), submenu: moveEntries(t) },
            { separator: true },
            { text: qsTr("Delete"), icon: "delete", danger: true, run: () => deleteConfirm.open() }
        ];
    }
    function moveEntries(t) {
        return TasksEngine.lists.filter(l => !l.readOnly).map(l => ({ text: l.name, radio: true, checked: l.uid === t.listUid, run: () => TasksEngine.moveTo(t.key, l.uid) }));
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: TasksEngine.problem.length > 0
            Layout.fillWidth: true
            implicitHeight: 40
            color: UI.Style.warningSoft
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: TasksEngine.problem
                size: 13
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: 12
            spacing: 10

            Ribbon {
                id: ribbon
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 12

                // Home
                RowLayout {
                    visible: ribbon.current === "home"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 2

                    UI.Button {
                        kind: "primary"
                        size: "small"
                        text: qsTr("New task")
                        iconName: "plus"
                        enabled: TasksEngine.ready
                        Layout.rightMargin: 6
                        onClicked: newTask.forceActiveFocus()
                    }
                    CommandButton {
                        text: page.current.done ? qsTr("Mark as not done") : qsTr("Mark complete")
                        iconName: "check"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: TasksEngine.setDone(page.current.key, !page.current.done)
                    }
                    CommandButton {
                        text: qsTr("Important")
                        iconName: "star"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: TasksEngine.setImportant(page.current.key, !page.current.important)
                    }
                    RibbonDivider {}
                    CommandButton {
                        id: dueButton
                        text: qsTr("Due date")
                        iconName: "calendar"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: page.popupMenu(page.dueEntries((d, a) => page.setDueFor(page.current.key, d, a), !!page.current.dueText), dueButton)
                    }
                    CommandButton {
                        id: remindButton
                        text: qsTr("Remind me")
                        iconName: "bell"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: page.popupMenu(page.reminderEntries(d => TasksEngine.setReminder(page.current.key, d), page.current.reminder), remindButton)
                    }
                    CommandButton {
                        text: page.current.myDay ? qsTr("Remove from My day") : qsTr("Add to My day")
                        iconName: "sun"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: TasksEngine.setMyDay(page.current.key, !page.current.myDay)
                    }
                    RibbonDivider {}
                    CommandButton {
                        id: moveButton
                        text: qsTr("Move to list")
                        iconName: "folder"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: page.popupMenu(page.moveEntries(page.current), moveButton)
                    }
                    CommandButton {
                        text: qsTr("Delete")
                        iconName: "trash"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: deleteConfirm.open()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    CommandButton {
                        iconOnly: true
                        iconName: "refresh"
                        tip: qsTr("Check for changes")
                        spinning: TasksEngine.syncing
                        onClicked: {
                            TasksEngine.refresh();
                            MailEngine.findFlagged();
                        }
                    }
                }

                // View
                RowLayout {
                    visible: ribbon.current === "view"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 2
                    CommandButton {
                        text: qsTr("Sort")
                        iconName: "sort-down"
                        menu: sortMenu
                    }
                    CommandButton {
                        text: prefs.showCompleted ? qsTr("Completed: shown") : qsTr("Completed: hidden")
                        iconName: "check"
                        onClicked: prefs.showCompleted = !prefs.showCompleted
                    }
                    RibbonDivider {}
                    CommandButton {
                        text: qsTr("Accounts")
                        iconName: "people"
                        onClicked: page.manageAccounts()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                // Help
                RowLayout {
                    visible: ribbon.current === "help"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 2
                    CommandButton {
                        text: qsTr("Help with Tasks")
                        iconName: "question"
                        onClicked: Qt.openUrlExternally("https://help.wildos.io/using/tasks/")
                    }
                    CommandButton {
                        text: qsTr("Report a problem")
                        iconName: "warning"
                        onClicked: Qt.openUrlExternally("wildos-report:mail")
                    }
                    CommandButton {
                        text: qsTr("Send feedback")
                        iconName: "send"
                        onClicked: Qt.openUrlExternally("https://github.com/wildorigins/wildos/discussions")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.rightMargin: 12
                spacing: 10

                // The views and the lists.
                ColumnLayout {
                    Layout.preferredWidth: 236
                    Layout.maximumWidth: 236
                    Layout.fillHeight: true
                    spacing: 0

                    QQC.ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 6
                        clip: true
                        contentWidth: availableWidth
                        QQC.ScrollBar.vertical: UI.ScrollBar {}

                        ColumnLayout {
                            width: parent.width
                            spacing: 0

                            Repeater {
                                model: [
                                    { id: "myday", name: qsTr("My day"), icon: "sun" },
                                    { id: "important", name: qsTr("Important"), icon: "star" },
                                    { id: "planned", name: qsTr("Planned"), icon: "calendar" },
                                    { id: "all", name: qsTr("All tasks"), icon: "tasks" },
                                    { id: "flagged", name: qsTr("Flagged email"), icon: "flag" }
                                ]
                                delegate: NavRow {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    iconName: modelData.icon
                                    text: modelData.name
                                    count: modelData.id === "flagged" ? page.flagged.length : TasksEngine.revision >= 0 ? TasksEngine.count(modelData.id) : 0
                                    current: page.view === modelData.id
                                    onClicked: page.pick(modelData.id)
                                }
                            }

                            NavHeading {
                                id: listsHeading
                                Layout.fillWidth: true
                                text: qsTr("Lists")
                            }
                            Repeater {
                                model: listsHeading.expanded ? TasksEngine.lists : []
                                delegate: NavRow {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    iconName: "folder"
                                    text: modelData.name
                                    count: modelData.count
                                    current: page.view === modelData.uid
                                    onClicked: page.pick(modelData.uid)
                                    onMenuRequested: {
                                        page.pick(modelData.uid);
                                        page.popupMenu(listMenuEntries(), null);
                                    }
                                }
                            }
                            Item {
                                visible: listsHeading.expanded
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                implicitHeight: 30
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 16
                                    spacing: 8
                                    MailIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "plus"
                                        size: 15
                                        color: newListMouse.containsMouse ? UI.Style.iconInk : UI.Style.iconMuted
                                    }
                                    UI.Label {
                                        text: qsTr("New list")
                                        size: 13
                                        color: newListMouse.containsMouse ? UI.Style.ink : UI.Style.inkMuted
                                    }
                                }
                                MouseArea {
                                    id: newListMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: listDialog.ask("")
                                }
                            }
                        }
                    }

                    SyncNote {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.topMargin: 8
                        iconName: "repeat"
                        iconColor: UI.Style.iconInk
                        title: TasksEngine.lists.some(l => l.account) ? qsTr("Synced with your accounts") : qsTr("Kept on this PC")
                        detail: TasksEngine.lists.some(l => l.account) ? qsTr("Tasks follow you to your phone") : qsTr("Add an account to see them on your phone")
                        onClicked: page.manageAccounts()
                    }
                }

                // The tasks.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: UI.Style.surfaceRest
                    border.width: 1
                    border.color: UI.Style.borderSubtle
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            Layout.leftMargin: 16
                            Layout.rightMargin: 10
                            spacing: 8
                            MailIcon {
                                name: page.viewIcon
                                size: 18
                                strokeWidth: 1.8
                                color: UI.Style.iconInk
                            }
                            UI.Label {
                                text: page.viewTitle
                                size: 16
                                font.weight: UI.Style.weightSemiBold
                                elide: Text.ElideRight
                                Layout.maximumWidth: 300
                            }
                            UI.Label {
                                visible: page.view === "myday"
                                Layout.leftMargin: 4
                                text: Formats.weekdayAndDay(new Date())
                                size: 13
                                color: UI.Style.inkMuted
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            CommandButton {
                                id: sortButton
                                iconOnly: true
                                visible: page.view !== "flagged"
                                iconName: "sort-down"
                                iconColor: UI.Style.iconMuted
                                tip: qsTr("Sort")
                                onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4)
                            }
                            CommandButton {
                                id: viewMore
                                iconOnly: true
                                iconName: "more"
                                iconColor: UI.Style.iconMuted
                                tip: qsTr("More")
                                onClicked: page.popupMenu(listMenuEntries(), viewMore)
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            implicitHeight: 1
                            color: UI.Style.borderSubtle
                        }

                        // Add a task.
                        Rectangle {
                            visible: page.view !== "flagged" && (page.smartView || !page.currentList.readOnly)
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.topMargin: 12
                            Layout.bottomMargin: 10
                            implicitHeight: 40
                            radius: 8
                            color: UI.Style.surfaceControl
                            border.width: 1
                            border.color: newTask.activeFocus ? UI.Style.accent : UI.Style.borderSubtle

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 6
                                spacing: 12
                                MailIcon {
                                    name: "plus"
                                    size: 17
                                    strokeWidth: 1.8
                                    color: UI.Style.iconInk
                                }
                                TextInput {
                                    id: newTask
                                    Layout.fillWidth: true
                                    color: UI.Style.ink
                                    selectionColor: UI.Style.textSelection
                                    selectedTextColor: UI.Style.ink
                                    font.family: UI.Style.fontFamily
                                    font.pixelSize: UI.Style.px(13.5)
                                    font.letterSpacing: UI.Style.tracking(UI.Style.px(13.5))
                                    clip: true
                                    enabled: TasksEngine.ready
                                    Accessible.role: Accessible.EditableText
                                    Accessible.name: qsTr("Add a task")
                                    onAccepted: page.addTask()
                                    Keys.onEscapePressed: {
                                        text = "";
                                        page.pending = ({});
                                        focus = false;
                                    }
                                    Keys.onPressed: event => {
                                        if (newTaskMenu.isMenuKey(event)) {
                                            newTaskMenu.openAtCaret();
                                            event.accepted = true;
                                        }
                                    }

                                    UI.TextMenu {
                                        id: newTaskMenu
                                    }
                                    UI.Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: newTask.text.length === 0
                                        text: qsTr("Add a task")
                                        size: 13.5
                                        color: UI.Style.inkMuted
                                    }
                                }
                                CommandButton {
                                    id: newDue
                                    iconOnly: true
                                    iconName: "calendar"
                                    iconColor: page.pending.due ? UI.Style.iconInk : UI.Style.iconMuted
                                    tip: page.pending.due ? qsTr("Due %1").arg(Formats.weekdayAndDay(page.pending.due)) : qsTr("Add due date")
                                    onClicked: page.popupMenu(page.dueEntries((d, a) => {
                                        const p = Object.assign({}, page.pending);
                                        if (isNaN(d.getTime())) {
                                            delete p.due;
                                        } else {
                                            p.due = d;
                                            p.dueAllDay = a;
                                        }
                                        page.pending = p;
                                    }, !!page.pending.due), newDue)
                                }
                                CommandButton {
                                    id: newRemind
                                    iconOnly: true
                                    iconName: "bell"
                                    iconColor: page.pending.reminder ? UI.Style.iconInk : UI.Style.iconMuted
                                    tip: page.pending.reminder ? qsTr("Remind me %1").arg(Formats.weekdayAndDay(page.pending.reminder) + ", " + Formats.time(page.pending.reminder)) : qsTr("Remind me")
                                    onClicked: page.popupMenu(page.reminderEntries(d => {
                                        const p = Object.assign({}, page.pending);
                                        if (isNaN(d.getTime())) {
                                            delete p.reminder;
                                        } else {
                                            p.reminder = d;
                                        }
                                        page.pending = p;
                                    }, !!page.pending.reminder), newRemind)
                                }
                                CommandButton {
                                    id: newRepeat
                                    iconOnly: true
                                    iconName: "repeat"
                                    iconColor: page.pending.repeat && page.pending.repeat !== "none" ? UI.Style.iconInk : UI.Style.iconMuted
                                    tip: page.pending.repeat && page.pending.repeat !== "none" ? page.repeatText(page.pending.repeat) : qsTr("Repeat")
                                    onClicked: page.popupMenu(page.repeatEntries(r => {
                                        const p = Object.assign({}, page.pending);
                                        p.repeat = r;
                                        page.pending = p;
                                    }, page.pending.repeat), newRepeat)
                                }
                            }
                        }

                        QQC.ScrollView {
                            id: taskScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: page.view === "flagged" ? 12 : 0
                            clip: true
                            contentWidth: availableWidth
                            QQC.ScrollBar.vertical: UI.ScrollBar {}

                            ColumnLayout {
                                width: taskScroll.availableWidth
                                spacing: 4

                                Repeater {
                                    model: page.view === "flagged" ? page.flaggedRows : page.openRows
                                    delegate: TaskRow {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 10
                                        Layout.rightMargin: 10
                                        task: modelData
                                        fromMail: page.view === "flagged"
                                        current: page.openKey === modelData.key
                                        showList: page.smartView || page.view !== modelData.listUid
                                        showMyDay: page.view !== "myday"
                                        onPicked: page.rowPicked(modelData.key, fromMail, false)
                                        onMenuRequested: page.rowPicked(modelData.key, fromMail, true)
                                        onTicked: page.rowTicked(modelData, fromMail)
                                        onStarred: TasksEngine.setImportant(modelData.key, !modelData.important)
                                    }
                                }

                                // Completed, folded or open.
                                Item {
                                    visible: page.view !== "flagged" && page.doneRows.length > 0
                                    Layout.fillWidth: true
                                    implicitHeight: 38
                                    Row {
                                        x: 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.verticalCenterOffset: 2
                                        spacing: 8
                                        MailIcon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: prefs.showCompleted ? "chevron-down" : "chevron-right"
                                            size: 13
                                            color: UI.Style.iconMuted
                                        }
                                        UI.Label {
                                            text: qsTr("Completed")
                                            size: 13
                                            font.weight: UI.Style.weightSemiBold
                                        }
                                        UI.Label {
                                            text: page.doneRows.length
                                            size: 13
                                            color: UI.Style.inkMuted
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: prefs.showCompleted = !prefs.showCompleted
                                    }
                                }
                                Repeater {
                                    model: prefs.showCompleted && page.view !== "flagged" ? page.doneRows : []
                                    delegate: TaskRow {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 10
                                        Layout.rightMargin: 10
                                        task: modelData
                                        current: page.openKey === modelData.key
                                        onPicked: page.rowPicked(modelData.key, false, false)
                                        onMenuRequested: page.rowPicked(modelData.key, false, true)
                                        onTicked: page.rowTicked(modelData, false)
                                    }
                                }
                                Item {
                                    implicitHeight: 8
                                }
                            }
                        }
                    }

                    UI.Label {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 30
                        width: parent.width - 80
                        visible: TasksEngine.ready && (page.view === "flagged" ? page.flaggedRows.length === 0 : page.openRows.length === 0 && page.doneRows.length === 0)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: page.filterText ? qsTr("No tasks match \"%1\".").arg(page.filterText)
                            : page.view === "myday" ? qsTr("Nothing in My day. Add a task, or add one from another list with Add to My day.")
                            : page.view === "important" ? qsTr("Star a task to see it here.")
                            : page.view === "planned" ? qsTr("Tasks with a due date show here.")
                            : page.view === "flagged" ? qsTr("Flag a message in Mail and it shows here as a task.")
                            : qsTr("No tasks here yet.")
                        size: 13
                        color: UI.Style.inkMuted
                    }
                    UI.Spinner {
                        anchors.centerIn: parent
                        visible: !TasksEngine.ready && TasksEngine.problem.length === 0
                        running: visible
                    }
                }

                // The task chosen.
                Rectangle {
                    Layout.preferredWidth: 340
                    Layout.fillHeight: true
                    radius: 10
                    color: UI.Style.surfaceRest
                    border.width: 1
                    border.color: UI.Style.borderSubtle
                    clip: true

                    UI.Label {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        visible: page.current.key === undefined
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Choose a task to see its steps, dates and notes")
                        size: 13
                        color: UI.Style.inkMuted
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        visible: page.current.key !== undefined
                        spacing: 0

                        QQC.ScrollView {
                            id: detailScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: availableWidth
                            QQC.ScrollBar.vertical: UI.ScrollBar {}

                            ColumnLayout {
                                width: detailScroll.availableWidth
                                spacing: 0

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 18
                                    Layout.rightMargin: 12
                                    Layout.topMargin: 16
                                    Layout.bottomMargin: 4
                                    spacing: 12
                                    Item {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 4
                                        implicitWidth: 22
                                        implicitHeight: 22
                                        MailIcon {
                                            anchors.centerIn: parent
                                            name: page.current.done ? "check-circle" : "circle"
                                            size: 22
                                            strokeWidth: 1.6
                                            color: page.current.done || bigTick.containsMouse ? UI.Style.accent : UI.Style.iconInk
                                        }
                                        MouseArea {
                                            id: bigTick
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: !page.current.readOnly
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: TasksEngine.setDone(page.current.key, !page.current.done)
                                        }
                                    }
                                    QQC.TextArea {
                                        id: titleEdit
                                        Layout.fillWidth: true
                                        readOnly: page.current.readOnly === true
                                        wrapMode: TextEdit.Wrap
                                        color: UI.Style.ink
                                        selectionColor: UI.Style.textSelection
                                        selectedTextColor: UI.Style.ink
                                        font.family: UI.Style.fontFamily
                                        font.pixelSize: UI.Style.px(17)
                                        font.weight: UI.Style.weightSemiBold
                                        font.strikeout: page.current.done === true
                                        padding: 2
                                        background: null
                                        Keys.onReturnPressed: focus = false
                                        Keys.onEnterPressed: focus = false
                                        function save() {
                                            if (text.trim() && text.trim() !== page.current.rawTitle) {
                                                TasksEngine.setTitle(page.current.key, text);
                                            }
                                        }
                                        // The text menu holds focus while it is open; the
                                        // title is saved once typing is over.
                                        onActiveFocusChanged: if (!activeFocus && !titleMenu.opened) save()
                                        Keys.onPressed: event => {
                                            if (titleMenu.isMenuKey(event)) {
                                                titleMenu.openAtCaret();
                                                event.accepted = true;
                                            }
                                        }

                                        UI.TextMenu {
                                            id: titleMenu
                                            onFocusLeft: titleEdit.save()
                                        }
                                    }
                                    UI.IconButton {
                                        Layout.alignment: Qt.AlignTop
                                        iconName: "star"
                                        iconColor: page.current.important ? MailIcons.star : UI.Style.iconInk
                                        tip: page.current.important ? qsTr("Not important") : qsTr("Important")
                                        enabled: !page.current.readOnly
                                        onClicked: TasksEngine.setImportant(page.current.key, !page.current.important)
                                    }
                                }

                                // Steps.
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 52
                                    Layout.rightMargin: 18
                                    spacing: 0
                                    Repeater {
                                        model: page.current.steps || []
                                        delegate: Item {
                                            id: step
                                            required property var modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            implicitHeight: Math.max(32, stepText.implicitHeight + 14)
                                            function toggled() {
                                                const steps = page.current.steps.map(s => ({ text: s.text, done: s.done }));
                                                steps[index].done = !steps[index].done;
                                                TasksEngine.setSteps(page.current.key, steps);
                                            }
                                            MouseArea {
                                                id: stepMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                            }
                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: 12
                                                Item {
                                                    implicitWidth: 18
                                                    implicitHeight: 18
                                                    MailIcon {
                                                        anchors.centerIn: parent
                                                        name: step.modelData.done ? "check-circle" : "circle"
                                                        size: 18
                                                        strokeWidth: 1.6
                                                        color: step.modelData.done ? UI.Style.accent : UI.Style.iconMuted
                                                    }
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        anchors.margins: -4
                                                        enabled: !page.current.readOnly
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: step.toggled()
                                                    }
                                                }
                                                UI.Label {
                                                    id: stepText
                                                    Layout.fillWidth: true
                                                    text: step.modelData.text
                                                    size: 13
                                                    wrapMode: Text.Wrap
                                                    font.strikeout: step.modelData.done
                                                    color: step.modelData.done ? UI.Style.inkMuted : UI.Style.ink
                                                }
                                                UI.IconButton {
                                                    id: removeStep
                                                    visible: (stepMouse.containsMouse || removeStep.hovered) && !page.current.readOnly
                                                    implicitWidth: 26
                                                    implicitHeight: 26
                                                    iconName: "close"
                                                    iconSize: 13
                                                    tip: qsTr("Remove step")
                                                    onClicked: TasksEngine.setSteps(page.current.key, page.current.steps.filter((_, i) => i !== step.index))
                                                }
                                            }
                                        }
                                    }
                                    RowLayout {
                                        visible: !page.current.readOnly
                                        Layout.fillWidth: true
                                        implicitHeight: 32
                                        spacing: 12
                                        MailIcon {
                                            name: "plus"
                                            size: 18
                                            strokeWidth: 1.6
                                            color: UI.Style.iconInk
                                        }
                                        TextInput {
                                            id: newStep
                                            Layout.fillWidth: true
                                            color: UI.Style.ink
                                            selectionColor: UI.Style.textSelection
                                            selectedTextColor: UI.Style.ink
                                            font.family: UI.Style.fontFamily
                                            font.pixelSize: UI.Style.px(13)
                                            clip: true
                                            Accessible.role: Accessible.EditableText
                                            Accessible.name: qsTr("Add a step")
                                            Keys.onPressed: event => {
                                                if (newStepMenu.isMenuKey(event)) {
                                                    newStepMenu.openAtCaret();
                                                    event.accepted = true;
                                                }
                                            }

                                            UI.TextMenu {
                                                id: newStepMenu
                                            }
                                            onAccepted: {
                                                const t = text.trim();
                                                if (t) {
                                                    TasksEngine.setSteps(page.current.key, (page.current.steps || []).concat([{ text: t, done: false }]));
                                                    text = "";
                                                }
                                            }
                                            UI.Label {
                                                anchors.verticalCenter: parent.verticalCenter
                                                visible: newStep.text.length === 0
                                                text: qsTr("Add a step")
                                                size: 13
                                                color: UI.Style.ink
                                            }
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.margins: 14
                                    Layout.topMargin: 10
                                    spacing: 6

                                    OptionCard {
                                        iconName: "sun"
                                        title: page.current.myDay ? qsTr("Added to My day") : qsTr("Add to My day")
                                        clearable: page.current.myDay === true
                                        onClicked: TasksEngine.setMyDay(page.current.key, true)
                                        onCleared: TasksEngine.setMyDay(page.current.key, false)
                                    }
                                    OptionCard {
                                        id: remindCard
                                        iconName: "bell"
                                        readonly property date at: page.current.reminderAt || new Date(NaN)
                                        readonly property bool isSet: !isNaN(at.getTime())
                                        title: isSet ? qsTr("Remind me at %1").arg(Formats.time(at)) : qsTr("Remind me")
                                        detail: isSet ? Formats.weekdayAndDay(at) : ""
                                        clearable: isSet
                                        onClicked: page.popupMenu(page.reminderEntries(d => TasksEngine.setReminder(page.current.key, d), isSet), remindCard)
                                        onCleared: TasksEngine.setReminder(page.current.key, new Date(NaN))
                                    }
                                    OptionCard {
                                        id: dueCard
                                        iconName: "calendar"
                                        readonly property date due: page.current.due || new Date(NaN)
                                        readonly property bool isSet: !isNaN(due.getTime())
                                        title: isSet ? qsTr("Due %1").arg(Formats.weekdayAndDay(due) + (page.current.dueAllDay ? "" : ", " + Formats.time(due))) : qsTr("Add due date")
                                        titleColor: page.current.dueState ? UI.Style.danger : UI.Style.ink
                                        clearable: isSet
                                        onClicked: page.popupMenu(page.dueEntries((d, a) => page.setDueFor(page.current.key, d, a), isSet), dueCard)
                                        onCleared: page.setDueFor(page.current.key, new Date(NaN), true)
                                    }
                                    OptionCard {
                                        id: repeatCard
                                        iconName: "repeat"
                                        readonly property bool isSet: !!page.current.repeat && page.current.repeat !== "none"
                                        title: isSet ? page.repeatText(page.current.repeat) : qsTr("Repeat")
                                        clearable: isSet
                                        onClicked: page.popupMenu(page.repeatEntries(r => TasksEngine.setRepeat(page.current.key, r), page.current.repeat), repeatCard)
                                        onCleared: TasksEngine.setRepeat(page.current.key, "none")
                                    }
                                    OptionCard {
                                        id: listCard
                                        iconName: "folder"
                                        title: qsTr("List")
                                        detail: page.current.listName || ""
                                        onClicked: page.popupMenu(page.moveEntries(page.current), listCard)
                                    }
                                    Repeater {
                                        model: page.current.files || []
                                        delegate: OptionCard {
                                            required property var modelData
                                            required property int index
                                            iconName: "paperclip"
                                            title: modelData.name
                                            detail: modelData.size > 0 ? page.sizeText(modelData.size) : ""
                                            clearable: true
                                            onClicked: TasksEngine.openFile(page.current.key, index)
                                            onCleared: TasksEngine.removeFile(page.current.key, index)
                                        }
                                    }
                                    OptionCard {
                                        iconName: "paperclip"
                                        title: qsTr("Add a file")
                                        onClicked: fileDialog.open()
                                    }

                                    QQC.TextArea {
                                        id: notesEdit
                                        Layout.fillWidth: true
                                        Layout.topMargin: 4
                                        Layout.minimumHeight: 84
                                        readOnly: page.current.readOnly === true
                                        wrapMode: TextEdit.Wrap
                                        color: UI.Style.inkMuted
                                        selectionColor: UI.Style.textSelection
                                        selectedTextColor: UI.Style.ink
                                        font.family: UI.Style.fontFamily
                                        font.pixelSize: UI.Style.px(13)
                                        font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
                                        placeholderText: qsTr("Add a note")
                                        placeholderTextColor: UI.Style.inkPlaceholder
                                        padding: 12
                                        background: Rectangle {
                                            radius: 8
                                            color: UI.Style.surfaceRaised
                                            border.width: 1
                                            border.color: notesEdit.activeFocus ? UI.Style.accent : UI.Style.borderSubtle
                                        }
                                        function save() {
                                            if (text !== (page.current.notes || "")) {
                                                TasksEngine.setNotes(page.current.key, text);
                                            }
                                        }
                                        // The text menu holds focus while it is open; the
                                        // note is saved once typing is over.
                                        onActiveFocusChanged: if (!activeFocus && !notesMenu.opened) save()
                                        Keys.onPressed: event => {
                                            if (notesMenu.isMenuKey(event)) {
                                                notesMenu.openAtCaret();
                                                event.accepted = true;
                                            }
                                        }

                                        UI.TextMenu {
                                            id: notesMenu
                                            onFocusLeft: notesEdit.save()
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: UI.Style.borderSubtle
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            Layout.leftMargin: 18
                            Layout.rightMargin: 8
                            UI.Label {
                                Layout.fillWidth: true
                                text: {
                                    const c = page.current;
                                    const done = c.completed ? new Date(c.completed) : null;
                                    const made = c.created ? new Date(c.created) : null;
                                    if (c.done && done && !isNaN(done.getTime())) {
                                        return qsTr("Completed on %1").arg(Formats.weekdayAndDay(done));
                                    }
                                    return made && !isNaN(made.getTime()) ? qsTr("Created on %1").arg(Formats.weekdayAndDay(made)) : "";
                                }
                                size: 12
                                color: UI.Style.inkMuted
                                elide: Text.ElideRight
                            }
                            UI.IconButton {
                                iconName: "trash"
                                tip: qsTr("Delete task")
                                enabled: !page.current.readOnly
                                onClicked: deleteConfirm.open()
                            }
                        }
                    }
                }
            }
        }
    }

    function listMenuEntries() {
        const l = currentList;
        const out = [
            { text: prefs.showCompleted ? qsTr("Hide completed tasks") : qsTr("Show completed tasks"), icon: "check", run: () => prefs.showCompleted = !prefs.showCompleted }
        ];
        if (l.uid && !l.readOnly) {
            out.push({ separator: true });
            out.push({ text: qsTr("Rename list"), icon: "rename", run: () => listDialog.ask(l.uid) });
            out.push({ text: qsTr("Delete list"), icon: "delete", danger: true, run: () => deleteListConfirm.open() });
        }
        return out;
    }

    UI.Menu {
        id: dynamicMenu
    }
    UI.Menu {
        id: sortMenu
        entries: [
            { text: qsTr("Newest first"), radio: true, checked: prefs.sort === "created", run: () => prefs.sort = "created" },
            { text: qsTr("Due date"), radio: true, checked: prefs.sort === "due", run: () => prefs.sort = "due" },
            { text: qsTr("Importance"), radio: true, checked: prefs.sort === "important", run: () => prefs.sort = "important" },
            { text: qsTr("Alphabetically"), radio: true, checked: prefs.sort === "title", run: () => prefs.sort = "title" }
        ]
    }

    DueDialog {
        id: duePicker
        property var apply: null
        onChosen: function (when, allDay) {
            if (apply) {
                apply(when, allDay);
            }
        }
    }
    DueDialog {
        id: reminderPicker
        property var apply: null
        needsTime: true
        onChosen: function (when) {
            if (apply) {
                apply(when);
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Add a file")
        fileMode: FileDialog.OpenFile
        onAccepted: TasksEngine.addFile(page.current.key, selectedFile)
    }

    UI.Dialog {
        id: deleteConfirm
        title: qsTr("Delete \"%1\"?").arg(page.current.rawTitle || "")
        text: qsTr("It's deleted from %1 too, and from every device that syncs with it.").arg(page.current.listName || "")
        iconName: "warning"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: deleteConfirm.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Delete")
                onClicked: {
                    TasksEngine.remove(page.current.key);
                    page.openKey = "";
                    page.current = ({});
                    deleteConfirm.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: deleteListConfirm
        title: qsTr("Delete %1?").arg(page.currentList.name || "")
        text: page.currentList.account ? qsTr("The list and its tasks are deleted from %1 too.").arg(page.currentList.account) : qsTr("The list and its tasks are deleted from this PC.")
        iconName: "warning"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: deleteListConfirm.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Delete list")
                onClicked: {
                    TasksEngine.removeList(page.currentList.uid);
                    page.pick("myday");
                    deleteListConfirm.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: listDialog
        property string listUid
        function ask(uid) {
            listUid = uid;
            const l = TasksEngine.lists.find(x => x.uid === uid);
            listName.text = l ? l.name : "";
            open();
            listName.forceActiveFocus();
            listName.selectAll();
        }
        title: listUid ? qsTr("Rename list") : qsTr("New list")
        text: listUid ? "" : qsTr("New lists are kept on this PC. Lists made on your phone or on your account's website show up here by themselves.")
        UI.TextField {
            id: listName
            Layout.fillWidth: true
            placeholderText: qsTr("List name")
            onAccepted: listSave.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: listDialog.close()
            },
            UI.Button {
                id: listSave
                kind: "primary"
                text: listDialog.listUid ? qsTr("Rename") : qsTr("Create")
                enabled: listName.text.trim().length > 0
                onClicked: {
                    if (listDialog.listUid) {
                        TasksEngine.renameList(listDialog.listUid, listName.text);
                    } else {
                        TasksEngine.addList(listName.text);
                    }
                    listDialog.close();
                }
            }
        ]
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 60
    }
}
