import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Wild OS Mail (MailMicrosoft.dc and the boards after it): the title bar
// with search, the app rail, the ribbon, then folders, messages and the
// reading pane, which turns into the message being written.
UI.AppWindow {
    id: window

    property bool composing: false
    property bool showSettings: false
    // Which tab the rail shows: mail, calendar, people or tasks.
    property string section: "mail"
    readonly property string sectionName: section === "calendar" ? qsTr("Calendar") : section === "people" ? qsTr("People")
        : section === "tasks" ? qsTr("Tasks") : qsTr("Mail")

    width: 1384
    height: 802
    minimumWidth: 960
    // Tall enough for every step of adding an account without scrolling.
    minimumHeight: 640
    visible: true
    title: sectionName

    // From main.cpp: Calendar's own launcher, or a reminder's Open.
    function showCalendar(key, day) {
        sectionFromOutside = true;
        section = "calendar";
        sectionFromOutside = false;
        if (key && key.length > 0) {
            Qt.callLater(() => calendarLoader.item.showEvent(key, day));
        }
    }
    // From main.cpp: People or Tasks from the command line, or a task's
    // reminder.
    function showSection(name) {
        if (["mail", "calendar", "people", "tasks"].indexOf(name) >= 0) {
            sectionFromOutside = true;
            section = name;
            sectionFromOutside = false;
        }
    }
    // People's and Tasks' accounts are Calendar's.
    function manageAccounts() {
        section = "calendar";
        Qt.callLater(() => calendarLoader.item.showAccounts());
    }
    // A message from People's recent email or a flagged-email task.
    function showMessage(key) {
        const parts = key.split("\x1f");
        section = "mail";
        showSettings = false;
        composing = false;
        MailEngine.openFolder(parts[0], parts[1]);
        MailEngine.selected = key;
    }
    // Mail's Google and Microsoft accounts, for Calendar's and People's bar
    // offering their calendars and contacts.
    readonly property var googleAccounts: MailEngine.accounts.filter(a => a.auth === "google" || a.auth === "microsoft")
    // A Microsoft account needs no second sign-in: Mail's covers calendars.
    function addAccountCalendar(email) {
        const account = MailEngine.accounts.find(a => a.email === email);
        if (account && account.auth === "microsoft") {
            MailEngine.addMicrosoftCalendar(account.id);
            return;
        }
        window.calendarSignIn = email;
        GoogleSignIn.startCalendar(email);
    }

    // A mailto: link, from main.cpp.
    function compose(data) {
        section = "mail";
        showSettings = false;
        composing = true;
        composePane.start(data || {});
    }
    function reply(mode) {
        if (!readingPane.view.key) {
            return;
        }
        compose(readingPane.view.replyData(mode));
    }
    function selectedKey() {
        return MailEngine.selected;
    }
    // Read or unread for everything selected: all read if any is unread.
    function toggleReadSelection() {
        const keys = MailEngine.selection;
        let anyUnread = false;
        for (const k of keys) {
            const i = MailEngine.messages.indexOf(k);
            if (i >= 0 && MailEngine.messages.unreadAt(i)) {
                anyUnread = true;
            }
        }
        MailEngine.setReadKeys(keys, anyUnread);
    }
    function toggleFlagSelection() {
        const keys = MailEngine.selection;
        let anyUnflagged = false;
        for (const k of keys) {
            const i = MailEngine.messages.indexOf(k);
            if (i >= 0 && !MailEngine.messages.flaggedAt(i)) {
                anyUnflagged = true;
            }
        }
        MailEngine.setFlaggedKeys(keys, anyUnflagged);
    }
    function openMessage(key) {
        messageWindow.createObject(null, { messageKey: key, main: window });
    }
    function openLink(url) {
        if (url.startsWith("mailto:")) {
            window.compose({ to: [url.substring(7).split("?")[0]] });
        } else {
            linkConfirm.ask(url);
        }
    }
    // The account's signature where the cursor is, or Settings to make one.
    function insertSignature() {
        const account = MailEngine.account(composePane.composer.accountId);
        if (!account.signature) {
            settingsPage.section = "writing";
            window.showSettings = true;
            toast.show(qsTr("Write your signature here, then add it from the message."));
            return;
        }
        composePane.formatter.insertText("\n" + account.signature);
    }

    Component {
        id: messageWindow
        MessageWindow {
            onClosing: destroy()
        }
    }
    function snoozeUntil(choice) {
        const d = new Date();
        if (choice === "later") {
            d.setHours(d.getHours() + 3);
        } else if (choice === "tomorrow") {
            d.setDate(d.getDate() + 1);
            d.setHours(8, 0, 0, 0);
        } else if (choice === "weekend") {
            d.setDate(d.getDate() + ((6 - d.getDay() + 7) % 7 || 7));
            d.setHours(8, 0, 0, 0);
        } else {
            d.setDate(d.getDate() + ((8 - d.getDay()) % 7 || 7));
            d.setHours(8, 0, 0, 0);
        }
        return d;
    }

    Settings {
        id: prefs
        category: "Window"
        property string readingPane: "right"
        // The tab the person left Mail on, opened again next time unless Mail
        // is started for something else (Calendar's launcher, a mailto: link).
        property string section: "mail"
    }
    // True while a launcher or a reminder picks the tab. That isn't the
    // person leaving Mail there: opening Calendar from its own icon mustn't
    // make Mail's icon open Calendar from then on.
    property bool sectionFromOutside: false
    onSectionChanged: {
        if (!sectionFromOutside) {
            prefs.section = section;
        }
    }

    Connections {
        target: MailEngine
        function onNotice(text) {
            toast.show(text);
        }
        function onUndoable(text) {
            toast.hide();
            undoBar.show(text);
        }
    }
    Connections {
        target: MicrosoftSignIn
        function onSignedIn(address, refreshToken) {
            if (signInDialog.microsoftAccount.length > 0) {
                MailEngine.signInAgain(signInDialog.microsoftAccount, refreshToken);
                signInDialog.microsoftAccount = "";
            }
        }
        function onFailed(message) {
            if (signInDialog.microsoftAccount.length > 0) {
                toast.show(message);
                signInDialog.microsoftAccount = "";
            }
        }
    }
    // The address Calendar's bar is signing in, between asking Google and
    // its answer.
    property string calendarSignIn: ""
    Connections {
        target: GoogleSignIn
        function onSignedIn(address, refreshToken) {
            if (window.calendarSignIn.length > 0) {
                CalendarEngine.addGoogleAccount(window.calendarSignIn, refreshToken);
                window.calendarSignIn = "";
                return;
            }
            if (signInDialog.googleAccount.length > 0) {
                MailEngine.signInAgain(signInDialog.googleAccount, refreshToken);
                signInDialog.googleAccount = "";
            }
        }
        function onFailed(message) {
            if (window.calendarSignIn.length > 0) {
                toast.show(message);
                window.calendarSignIn = "";
                return;
            }
            if (signInDialog.googleAccount.length > 0) {
                toast.show(message);
                signInDialog.googleAccount = "";
            }
        }
    }
    Connections {
        target: Notifier
        function onOpenRequested(key) {
            window.showMessage(key);
            window.raise();
            window.requestActivate();
        }
    }

    Shortcut {
        sequence: StandardKey.New
        enabled: window.section === "mail"
        onActivated: window.compose({})
    }
    Shortcut {
        sequence: "Ctrl+R"
        enabled: window.section === "mail" && (!window.composing && !window.showSettings)
        onActivated: window.reply("reply")
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        enabled: window.section === "mail" && (!window.composing && !window.showSettings)
        onActivated: window.reply("replyAll")
    }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: window.section === "mail" && (!window.composing && !window.showSettings)
        onActivated: window.reply("forward")
    }
    Shortcut {
        sequence: "Ctrl+E"
        onActivated: search.forceActiveFocus()
    }
    Shortcut {
        sequence: "F5"
        enabled: window.section === "mail"
        onActivated: MailEngine.refresh()
    }
    Shortcut {
        sequence: "Ctrl+Q"
        enabled: window.section === "mail" && (!window.composing)
        onActivated: MailEngine.setReadKeys(MailEngine.selection, true)
    }
    Shortcut {
        sequence: "Ctrl+U"
        enabled: window.section === "mail" && (!window.composing)
        onActivated: MailEngine.setReadKeys(MailEngine.selection, false)
    }
    Shortcut {
        sequence: StandardKey.Print
        enabled: window.section === "mail" && !window.composing && !window.showSettings && readingPane.hasMessage
        onActivated: readingPane.print()
    }
    // Only from the message list, never while typing anywhere else.
    Shortcut {
        sequence: "Backspace"
        enabled: window.section === "mail" && (messageList.listFocused && !window.composing)
        onActivated: MailEngine.archiveKeys(MailEngine.selection)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            id: titleBar
            Layout.fillWidth: true
            title: window.sectionName
            tinted: true
            frame: window.frame

            Item {
                anchors.fill: parent

                UI.SearchField {
                    id: search
                    anchors.centerIn: parent
                    width: Math.min(520, parent.width - 160)
                    height: 32
                    placeholderText: window.section === "calendar" ? qsTr("Search events") : window.section === "people" ? qsTr("Search people")
                        : window.section === "tasks" ? qsTr("Search tasks") : qsTr("Search mail and people")
                    onTextChanged: searchTimer.restart()
                    onEscapePressed: {
                        text = "";
                        if (window.section === "calendar") {
                            calendarLoader.item.clearSearch();
                        } else if (window.section === "people") {
                            peopleLoader.item.clearSearch();
                        } else if (window.section === "tasks") {
                            tasksLoader.item.clearSearch();
                        } else {
                            MailEngine.clearSearch();
                        }
                    }
                    onAccepted: {
                        searchTimer.stop();
                        if (window.section === "calendar") {
                            calendarLoader.item.search(text);
                        } else if (window.section === "people") {
                            peopleLoader.item.search(text);
                        } else if (window.section === "tasks") {
                            tasksLoader.item.search(text);
                        } else {
                            MailEngine.search(text);
                        }
                    }
                }
                Timer {
                    id: searchTimer
                    interval: 300
                    onTriggered: {
                        if (window.section === "calendar") {
                            calendarLoader.item.search(search.text);
                        } else if (window.section === "people") {
                            peopleLoader.item.search(search.text);
                        } else if (window.section === "tasks") {
                            tasksLoader.item.search(search.text);
                        } else if (search.text.trim().length > 0) {
                            MailEngine.search(search.text);
                        } else {
                            MailEngine.clearSearch();
                        }
                    }
                }
                CommandButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    iconOnly: true
                    iconName: "settings"
                    tip: qsTr("Settings")
                    onClicked: {
                        if (window.section === "calendar") {
                            calendarLoader.item.toggleSettings();
                            return;
                        }
                        if (window.section === "people" || window.section === "tasks") {
                            window.manageAccounts();
                            return;
                        }
                        window.composing = false;
                        window.showSettings = !window.showSettings;
                    }
                }
            }
        }

        // You're offline (MailMsStates.dc).
        Rectangle {
            visible: !MailEngine.online && MailEngine.hasAccounts && window.section === "mail"
            Layout.fillWidth: true
            implicitHeight: 40
            color: UI.Style.warningSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 12
                spacing: 10
                MailIcon {
                    name: "offline"
                    size: 16
                    color: UI.Style.warning
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("You're offline. New mail will arrive when you reconnect, and anything you send waits in the Outbox.")
                    size: 13
                    elide: Text.ElideRight
                }
                UI.Button {
                    size: "small"
                    text: qsTr("Try again")
                    onClicked: MailEngine.refresh()
                }
            }
        }

        // An account that needs its password again.
        Repeater {
            model: MailEngine.problems
            delegate: Rectangle {
                required property var modelData
                visible: MailEngine.online && window.section === "mail"
                Layout.fillWidth: true
                implicitHeight: 40
                color: UI.Style.warningSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 12
                    spacing: 10
                    MailIcon {
                        name: modelData.kind === "password" ? "key" : "warning"
                        size: 16
                        color: UI.Style.warning
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: modelData.name + ": " + modelData.text
                        size: 13
                        elide: Text.ElideRight
                    }
                    UI.Button {
                        size: "small"
                        text: modelData.kind === "password" ? qsTr("Sign in again") : qsTr("Try again")
                        onClicked: modelData.kind === "password" ? signInDialog.ask(modelData.accountId) : MailEngine.refresh()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            AppRail {
                Layout.fillHeight: true
                current: window.section
                squareCorner: window.maximised
                onPicked: function (app) {
                    search.text = "";
                    window.section = app;
                }
            }

            // Calendar, made the first time its tab is chosen.
            Loader {
                id: calendarLoader
                active: window.section === "calendar" || item !== null
                visible: window.section === "calendar"
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: CalendarPage {
                    mailAccounts: window.googleAccounts
                    onAddGoogleCalendar: function (email) {
                        window.addAccountCalendar(email);
                    }
                }
            }

            // People and Tasks, made the first time their tabs are chosen.
            Loader {
                id: peopleLoader
                active: window.section === "people" || item !== null
                visible: window.section === "people"
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: PeoplePage {
                    mailAccounts: window.googleAccounts
                    onAddGoogleAccount: function (email) {
                        window.addAccountCalendar(email);
                    }
                    onEmailTo: function (address) {
                        window.section = "mail";
                        window.compose({ to: [address] });
                    }
                    onNewEventWith: function (address) {
                        window.section = "calendar";
                        Qt.callLater(() => calendarLoader.item.newEvent(address ? { attendees: [address] } : {}));
                    }
                    onOpenMessage: function (key) {
                        window.showMessage(key);
                    }
                    onShareByEmail: function (file, name) {
                        window.section = "mail";
                        window.compose({ subject: name });
                        composePane.composer.addAttachment(file);
                    }
                    onManageAccounts: window.manageAccounts()
                }
            }
            Loader {
                id: tasksLoader
                active: window.section === "tasks" || item !== null
                visible: window.section === "tasks"
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: TasksPage {
                    onOpenMessage: function (key) {
                        window.showMessage(key);
                    }
                    onManageAccounts: window.manageAccounts()
                }
            }

            ColumnLayout {
                visible: window.section === "mail"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.bottomMargin: 12
                spacing: 10

                // The ribbon (MailMicrosoft.dc): Home, View and Help, or
                // Message while writing (MailMsCompose.dc).
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 12
                    spacing: 2
                    // Nothing on the ribbon works without an account.
                    visible: !window.showSettings && MailEngine.hasAccounts

                    Row {
                        id: tabs
                        property string current: "home"
                        leftPadding: 4
                        spacing: 22

                        Repeater {
                            model: window.composing ? [{ id: "message", name: qsTr("Message") }]
                                : [{ id: "home", name: qsTr("Home") }, { id: "view", name: qsTr("View") }, { id: "help", name: qsTr("Help") }]
                            delegate: Item {
                                id: tab
                                required property var modelData
                                readonly property bool active: window.composing || tabs.current === modelData.id
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
                            visible: !window.composing && tabs.current === "home"
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 2


                            UI.Button {
                                kind: "primary"
                                size: "small"
                                text: qsTr("New mail")
                                iconName: "pencil"
                                enabled: MailEngine.hasAccounts
                                Layout.rightMargin: 6
                                onClicked: window.compose({})
                            }
                            CommandButton {
                                text: qsTr("Delete")
                                iconName: "trash"
                                enabled: MailEngine.selection.length > 0
                                onClicked: MailEngine.removeKeys(MailEngine.selection)
                            }
                            CommandButton {
                                text: qsTr("Archive")
                                iconName: "archive"
                                enabled: MailEngine.selection.length > 0
                                onClicked: MailEngine.archiveKeys(MailEngine.selection)
                            }
                            CommandButton {
                                text: MailEngine.folderRole === "junk" ? qsTr("Not junk") : qsTr("Report")
                                iconName: "report"
                                enabled: MailEngine.selection.length > 0
                                onClicked: MailEngine.folderRole === "junk" ? MailEngine.notJunk(MailEngine.selected) : MailEngine.junkKeys(MailEngine.selection)
                            }
                            Rectangle {
                                implicitWidth: 1
                                implicitHeight: 22
                                Layout.leftMargin: 6
                                Layout.rightMargin: 6
                                color: UI.Style.borderSubtle
                            }
                            CommandButton {
                                id: moveButton
                                text: qsTr("Move to")
                                iconName: "move"
                                enabled: MailEngine.selection.length > 0
                                onClicked: moveMenu.openFor(moveButton)
                            }
                            Rectangle {
                                implicitWidth: 1
                                implicitHeight: 22
                                Layout.leftMargin: 6
                                Layout.rightMargin: 6
                                color: UI.Style.borderSubtle
                            }
                            CommandButton {
                                text: qsTr("Reply")
                                iconName: "reply"
                                enabled: MailEngine.selection.length > 0
                                onClicked: window.reply("reply")
                            }
                            CommandButton {
                                text: qsTr("Reply all")
                                iconName: "reply-all"
                                enabled: MailEngine.selection.length > 0
                                onClicked: window.reply("replyAll")
                            }
                            CommandButton {
                                text: qsTr("Forward")
                                iconName: "forward"
                                enabled: MailEngine.selection.length > 0
                                onClicked: window.reply("forward")
                            }
                            Rectangle {
                                implicitWidth: 1
                                implicitHeight: 22
                                Layout.leftMargin: 6
                                Layout.rightMargin: 6
                                color: UI.Style.borderSubtle
                            }
                            CommandButton {
                                text: qsTr("Read or unread")
                                iconName: "read-unread"
                                enabled: MailEngine.selection.length > 0
                                onClicked: window.toggleReadSelection()
                            }
                            CommandButton {
                                text: qsTr("Flag")
                                iconName: "flag"
                                enabled: MailEngine.selection.length > 0
                                onClicked: window.toggleFlagSelection()
                            }
                            CommandButton {
                                text: qsTr("Pin")
                                iconName: "pin"
                                enabled: MailEngine.selection.length > 0
                                onClicked: MailEngine.togglePin(MailEngine.selected)
                            }
                            CommandButton {
                                text: qsTr("Snooze")
                                iconName: "clock"
                                enabled: MailEngine.selection.length > 0
                                menu: snoozeMenu
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "refresh"
                                tip: qsTr("Check for new mail")
                                spinning: MailEngine.syncing
                                onClicked: MailEngine.refresh()
                            }
                        }

                        // View
                        RowLayout {
                            visible: !window.composing && tabs.current === "view"
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 2

                            CommandButton {
                                text: prefs.readingPane === "off" ? qsTr("Reading pane: off") : qsTr("Reading pane: on the right")
                                iconName: "layout"
                                onClicked: prefs.readingPane = prefs.readingPane === "off" ? "right" : "off"
                            }
                            CommandButton {
                                text: MailEngine.messages.conversations ? qsTr("Conversations: on") : qsTr("Conversations: off")
                                iconName: "list"
                                onClicked: MailEngine.messages.conversations = !MailEngine.messages.conversations
                            }
                            CommandButton {
                                text: qsTr("Filter")
                                iconName: "filter"
                                menu: viewFilterMenu
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        // Help
                        RowLayout {
                            visible: !window.composing && tabs.current === "help"
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 2

                            CommandButton {
                                text: qsTr("Help with Mail")
                                iconName: "question"
                                onClicked: Qt.openUrlExternally("https://help.wildos.io/using/mail/")
                            }
                            CommandButton {
                                text: qsTr("Keyboard shortcuts")
                                iconName: "font"
                                onClicked: shortcutsDialog.open()
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

                        // Message, while writing
                        RowLayout {
                            visible: window.composing
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 2

                            CommandButton {
                                iconOnly: true
                                iconName: "bold"
                                tip: qsTr("Bold")
                                onClicked: composePane.formatter.toggleBold()
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "italic"
                                tip: qsTr("Italic")
                                onClicked: composePane.formatter.toggleItalic()
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "underline"
                                tip: qsTr("Underline")
                                onClicked: composePane.formatter.toggleUnderline()
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "bullets"
                                tip: qsTr("Bullets")
                                onClicked: composePane.formatter.bullets()
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "numbering"
                                tip: qsTr("Numbering")
                                onClicked: composePane.formatter.numbering()
                            }
                            Rectangle {
                                implicitWidth: 1
                                implicitHeight: 22
                                Layout.leftMargin: 6
                                Layout.rightMargin: 6
                                color: UI.Style.borderSubtle
                            }
                            CommandButton {
                                text: qsTr("Attach file")
                                iconName: "paperclip"
                                onClicked: composePane.attach()
                            }
                            CommandButton {
                                text: qsTr("Link")
                                iconName: "link"
                                onClicked: linkDialog.open()
                            }
                            CommandButton {
                                text: qsTr("Signature")
                                iconName: "signature"
                                onClicked: window.insertSignature()
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
                    spacing: 10

                    FolderPane {
                        Layout.preferredWidth: 236
                        Layout.fillHeight: true
                        visible: MailEngine.hasAccounts && !window.showSettings
                        onAddAccount: addAccount.open()
                        onCreateFolder: function (accountId) {
                            newFolderDialog.ask(accountId);
                        }
                        onOpenPrivacy: {
                            settingsPage.section = "privacy";
                            window.showSettings = true;
                        }
                        onFolderMenu: function (accountId, fullName, name, role, from) {
                            folderMenu.accountId = accountId;
                            folderMenu.fullName = fullName;
                            folderMenu.name = name;
                            folderMenu.role = role;
                            if (from) {
                                folderMenu.popup(from, 16, from.height);
                            } else {
                                folderMenu.popup();
                            }
                        }
                        onCrossAccount: toast.show(qsTr("Messages can only be moved within the same account."))
                    }

                    MessageList {
                        id: messageList
                        Layout.preferredWidth: prefs.readingPane === "off" && !window.composing ? -1 : 392
                        Layout.fillWidth: prefs.readingPane === "off" && !window.composing && !MailEngine.selected
                        Layout.fillHeight: true
                        visible: MailEngine.hasAccounts && !window.showSettings && !(prefs.readingPane === "off" && MailEngine.selected && !window.composing)
                        onContextMenu: function (key, x, y) {
                            messageMenu.popup();
                        }
                        onOpenMessage: function (key) {
                            window.openMessage(key);
                        }
                    }

                    ReadingPane {
                        id: readingPane
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: MailEngine.hasAccounts && !window.composing && !window.showSettings
                            && (prefs.readingPane !== "off" || MailEngine.selected.length > 0)
                        onReply: function (mode) {
                            window.reply(mode);
                        }
                        onMoreMenu: function (x, y, from) {
                            messageMenu.popup(from, x, y);
                        }
                        onOpenLink: function (url) {
                            window.openLink(url);
                        }
                        onSaved: function (path) {
                            toast.show(qsTr("Saved to %1").arg(path));
                        }
                        onPrintFailed: toast.show(qsTr("The printer couldn't be reached."))
                    }

                    ComposePane {
                        id: composePane
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: window.composing && !window.showSettings
                        // A new message scales up from 96% as it fades in,
                        // 180 ms (MailMsMotion.dc), ready to type at once.
                        transformOrigin: Item.Center
                        onVisibleChanged: if (visible) composeIn.restart()
                        ParallelAnimation {
                            id: composeIn
                            NumberAnimation { target: composePane; property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
                            NumberAnimation { target: composePane; property: "scale"; from: UI.Style.reducedMotion ? 1 : 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
                        }
                        onClosed: window.composing = false
                        onProblem: function (text) {
                            toast.show(text);
                        }
                        onSentMessage: toast.show(MailEngine.online ? qsTr("Sending") : qsTr("Saved in the Outbox. It goes when you're back online."))
                    }

                    SettingsPage {
                        id: settingsPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: window.showSettings
                        // Fades and rises 8 px into place, 200 ms
                        // (MailMsMotion.dc); closing is instant.
                        transform: Translate {
                            id: settingsRise
                        }
                        onVisibleChanged: if (visible) settingsIn.restart()
                        ParallelAnimation {
                            id: settingsIn
                            NumberAnimation { target: settingsPage; property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.OutCubic }
                            NumberAnimation { target: settingsRise; property: "y"; from: UI.Style.reducedMotion ? 0 : 8; to: 0; duration: 200; easing.type: Easing.OutCubic }
                        }
                        readingPane: prefs.readingPane
                        onReadingPaneChanged: prefs.readingPane = readingPane
                        onCloseRequested: window.showSettings = false
                        onAddAccount: addAccount.open()
                        onSignIn: function (accountId) {
                            signInDialog.ask(accountId);
                        }
                    }

                    // No account yet.
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !MailEngine.hasAccounts && !window.showSettings

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(460, parent.width - 48)
                        spacing: 12
                        MailIcon {
                            Layout.alignment: Qt.AlignHCenter
                            name: "mail"
                            size: 56
                            strokeWidth: 1.2
                            color: UI.Style.iconMuted
                        }
                        UI.Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Bring your mail to Wild OS")
                            size: 22
                            font.weight: UI.Style.weightSemiBold
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Your mail stays with your provider and on this PC. Pictures that track you are blocked, and nothing runs inside a message.")
                            size: 13
                            color: UI.Style.inkMuted
                        }
                        UI.Button {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 8
                            kind: "primary"
                            size: "large"
                            text: qsTr("Add an account")
                            onClicked: addAccount.open()
                        }
                    }
                    }
                }
            }
        }
    }

    AddAccount {
        id: addAccount
    }

    UI.Menu {
        id: messageMenu
        entries: {
            const k = MailEngine.selected;
            const keys = MailEngine.selection;
            const several = keys.length > 1;
            return several ? [
                { text: qsTr("Mark as read"), run: () => MailEngine.setReadKeys(keys, true) },
                { text: qsTr("Mark as unread"), run: () => MailEngine.setReadKeys(keys, false) },
                { text: qsTr("Flag"), run: () => MailEngine.setFlaggedKeys(keys, true) },
                { separator: true },
                { text: qsTr("Archive"), run: () => MailEngine.archiveKeys(keys) },
                { text: qsTr("Move to"), submenu: moveEntries() },
                { text: qsTr("Report as junk"), run: () => MailEngine.junkKeys(keys) },
                { separator: true },
                { text: qsTr("Delete %1 messages").arg(keys.length), icon: "delete", danger: true, run: () => MailEngine.removeKeys(keys) }
            ] : [
                { text: qsTr("Open in a new window"), icon: "external", run: () => window.openMessage(k) },
                { separator: true },
                { text: qsTr("Reply"), icon: "back", run: () => window.reply("reply") },
                { text: qsTr("Reply all"), run: () => window.reply("replyAll") },
                { text: qsTr("Forward"), icon: "forward", run: () => window.reply("forward") },
                { separator: true },
                { text: qsTr("Read or unread"), run: () => MailEngine.toggleRead(k) },
                { text: qsTr("Flag"), run: () => MailEngine.toggleFlag(k) },
                { text: qsTr("Pin"), icon: "pin", run: () => MailEngine.togglePin(k) },
                { text: qsTr("Snooze"), submenu: snoozeEntries() },
                { separator: true },
                { text: qsTr("Archive"), run: () => MailEngine.archiveKeys([k]) },
                { text: qsTr("Move to"), submenu: moveEntries() },
                { text: MailEngine.folderRole === "junk" ? qsTr("Not junk") : qsTr("Report as junk"), run: () => MailEngine.folderRole === "junk" ? MailEngine.notJunk(k) : MailEngine.reportJunk(k) },
                { separator: true },
                { text: qsTr("Print"), icon: "printer", shortcut: "Ctrl+P", enabled: readingPane.hasMessage, run: () => readingPane.print() },
                { separator: true },
                { text: qsTr("Delete"), icon: "delete", danger: true, run: () => MailEngine.removeKeys([k]) }
            ];
        }
    }

    function snoozeEntries() {
        return [
            { text: qsTr("Later today"), run: () => MailEngine.snooze(MailEngine.selected, snoozeUntil("later")) },
            { text: qsTr("Tomorrow"), run: () => MailEngine.snooze(MailEngine.selected, snoozeUntil("tomorrow")) },
            { text: qsTr("This weekend"), run: () => MailEngine.snooze(MailEngine.selected, snoozeUntil("weekend")) },
            { text: qsTr("Next week"), run: () => MailEngine.snooze(MailEngine.selected, snoozeUntil("week")) }
        ];
    }
    function moveEntries() {
        const account = MailEngine.accountForKey(MailEngine.selected);
        const here = MailEngine.selected.split("\x1f")[1];
        return MailEngine.moveTargets(account).filter(f => f.fullName !== here).map(f => ({ text: "    ".repeat(Math.max(0, f.depth)) + f.name, run: () => MailEngine.moveKeys(MailEngine.selection, f.fullName) }));
    }

    UI.Menu {
        id: folderMenu
        property string accountId
        property string fullName
        property string name
        property string role
        readonly property bool special: role !== "folder"
        entries: [
            { text: qsTr("Mark all as read"), run: () => MailEngine.markFolderRead(folderMenu.accountId, folderMenu.fullName) },
            { text: qsTr("Empty folder"), visible: true, enabled: role === "trash" || role === "junk", run: () => emptyConfirm.ask(folderMenu.accountId, folderMenu.fullName, folderMenu.name) },
            { separator: true },
            { text: qsTr("Create new folder"), icon: "new-folder", run: () => newFolderDialog.ask(folderMenu.accountId) },
            { text: qsTr("Rename"), enabled: !special, run: () => renameDialog.ask(folderMenu.accountId, folderMenu.fullName, folderMenu.name) },
            { text: qsTr("Delete folder"), icon: "delete", danger: true, enabled: !special, run: () => deleteFolderConfirm.ask(folderMenu.accountId, folderMenu.fullName, folderMenu.name) }
        ]
    }

    UI.Dialog {
        id: renameDialog
        property string accountId
        property string fullName
        function ask(account, full, name) {
            accountId = account;
            fullName = full;
            renameField.text = name;
            open();
            renameField.forceActiveFocus();
            renameField.selectAll();
        }
        title: qsTr("Rename folder")
        UI.TextField {
            id: renameField
            Layout.fillWidth: true
            onAccepted: renameButton.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: renameDialog.close()
            },
            UI.Button {
                id: renameButton
                kind: "primary"
                text: qsTr("Rename")
                enabled: renameField.text.trim().length > 0 && renameField.text.indexOf("/") < 0
                onClicked: {
                    MailEngine.renameFolder(renameDialog.accountId, renameDialog.fullName, renameField.text);
                    renameDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: deleteFolderConfirm
        property string accountId
        property string fullName
        property string name
        function ask(account, full, n) {
            accountId = account;
            fullName = full;
            name = n;
            open();
        }
        title: qsTr("Delete %1?").arg(name)
        text: qsTr("The folder and the mail in it are deleted from your email provider too. This can't be undone.")
        iconName: "warning"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: deleteFolderConfirm.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Delete folder")
                onClicked: {
                    MailEngine.deleteFolder(deleteFolderConfirm.accountId, deleteFolderConfirm.fullName);
                    deleteFolderConfirm.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: emptyConfirm
        property string accountId
        property string fullName
        property string name
        function ask(account, full, n) {
            accountId = account;
            fullName = full;
            name = n;
            open();
        }
        title: qsTr("Empty %1?").arg(name)
        text: qsTr("Everything in it is deleted for good, from your email provider too.")
        iconName: "warning"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: emptyConfirm.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Empty")
                onClicked: {
                    MailEngine.emptyFolder(emptyConfirm.accountId, emptyConfirm.fullName);
                    emptyConfirm.close();
                }
            }
        ]
    }

    UI.Menu {
        id: snoozeMenu
        entries: window.snoozeEntries()
    }
    UI.Menu {
        id: moveMenu
        function openFor(item) {
            entries = window.moveEntries();
            popup(item, 0, item.height + 4);
        }
    }
    UI.Menu {
        id: viewFilterMenu
        entries: [
            { text: qsTr("All"), radio: true, checked: MailEngine.messages.filter === "all", run: () => MailEngine.messages.filter = "all" },
            { text: qsTr("Unread"), radio: true, checked: MailEngine.messages.filter === "unread", run: () => MailEngine.messages.filter = "unread" },
            { text: qsTr("Flagged"), radio: true, checked: MailEngine.messages.filter === "flagged", run: () => MailEngine.messages.filter = "flagged" },
            { text: qsTr("Has attachments"), radio: true, checked: MailEngine.messages.filter === "attachments", run: () => MailEngine.messages.filter = "attachments" }
        ]
    }

    // A link in a message opens in the browser after one look at where it
    // really goes: the words of a link and its address can disagree.
    UI.Dialog {
        id: linkConfirm
        property string url
        function ask(u) {
            url = u;
            open();
        }
        title: qsTr("Open this link?")
        text: url
        iconName: "external"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: linkConfirm.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Open")
                onClicked: {
                    Qt.openUrlExternally(linkConfirm.url);
                    linkConfirm.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: newFolderDialog
        property string accountId
        function ask(id) {
            accountId = id;
            folderName.text = "";
            open();
            folderName.forceActiveFocus();
        }
        title: qsTr("Create new folder")
        UI.TextField {
            id: folderName
            Layout.fillWidth: true
            placeholderText: qsTr("Folder name")
            onAccepted: createButton.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: newFolderDialog.close()
            },
            UI.Button {
                id: createButton
                kind: "primary"
                text: qsTr("Create")
                enabled: folderName.text.trim().length > 0
                onClicked: {
                    MailEngine.createFolder(newFolderDialog.accountId, folderName.text);
                    newFolderDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: signInDialog
        property string accountId
        // An account signed in with Google or Microsoft signs in again on
        // their page.
        property string googleAccount
        property string microsoftAccount
        function ask(id) {
            const account = MailEngine.account(id);
            if (account.auth === "google") {
                googleAccount = id;
                GoogleSignIn.start(account.email);
                toast.show(qsTr("Sign in on Google's page in your browser."));
                return;
            }
            if (account.auth === "microsoft") {
                microsoftAccount = id;
                MicrosoftSignIn.start(account.email);
                toast.show(qsTr("Sign in on Microsoft's page in your browser."));
                return;
            }
            accountId = id;
            signInPassword.text = "";
            open();
            signInPassword.forceActiveFocus();
        }
        title: qsTr("Sign in again")
        text: qsTr("Type the password for %1. For Gmail, iCloud, Yahoo and Fastmail, that's an app password.").arg(MailEngine.account(accountId).email || "")
        UI.TextField {
            id: signInPassword
            Layout.fillWidth: true
            revealable: true
            onAccepted: signInButton.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: signInDialog.close()
            },
            UI.Button {
                id: signInButton
                kind: "primary"
                text: qsTr("Sign in")
                enabled: signInPassword.text.length > 0
                onClicked: {
                    MailEngine.signInAgain(signInDialog.accountId, signInPassword.text);
                    signInPassword.text = "";
                    signInDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: linkDialog
        title: qsTr("Insert link")
        UI.TextField {
            id: linkUrl
            Layout.fillWidth: true
            placeholderText: "https://"
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: linkDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Insert")
                enabled: linkUrl.text.trim().length > 0
                onClicked: {
                    let u = linkUrl.text.trim();
                    if (!/^[a-z]+:/i.test(u)) {
                        u = "https://" + u;
                    }
                    composePane.formatter.insertLink(u, "");
                    linkUrl.text = "";
                    linkDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: shortcutsDialog
        title: qsTr("Keyboard shortcuts")
        text: qsTr("New mail: Ctrl+N\nReply: Ctrl+R\nReply all: Ctrl+Shift+R\nForward: Ctrl+Shift+F\nSend: Ctrl+Enter\nSearch: Ctrl+E\nMark as read: Ctrl+Q\nMark as unread: Ctrl+U\nArchive: Backspace\nDelete: Delete\nCheck for new mail: F5\nPrint: Ctrl+P\nMove an account up or down: Alt+Up, Alt+Down")
        buttons: [
            UI.Button {
                kind: "primary"
                text: qsTr("Close")
                onClicked: shortcutsDialog.close()
            }
        ]
    }

    UndoBar {
        id: undoBar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 61
    }
    // Ctrl Z puts back the last of the ten most recent moves and deletes;
    // in a text box it undoes typing as usual.
    Shortcut {
        sequence: StandardKey.Undo
        enabled: window.section === "mail" && !window.composing && !window.showSettings && MailEngine.canUndo
            && !(window.activeFocusItem && window.activeFocusItem.hasOwnProperty("selectByMouse"))
        onActivated: {
            undoBar.shown = false;
            MailEngine.undo();
        }
    }

    UI.Toast {
        id: toast
        onActionClicked: MailEngine.undo()
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 60
    }

    Component.onCompleted: {
        showSection(prefs.section);
        if (MailEngine.hasAccounts) {
            MailEngine.openLastFolder();
        }
    }
}
