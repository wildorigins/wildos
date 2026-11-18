import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// People (MailPeople.dc): the ribbon, then everyone, favourites, groups and
// each account's contacts on the left, A to Z in the middle, and the person
// chosen on the right with recent mail and what's coming up with them. A
// tab of the Mail window; the window around it has the title bar, its
// search and the app rail.
Item {
    id: page

    // Mail's Google accounts whose contacts aren't here yet, offered in a bar.
    property var mailAccounts: []
    signal addGoogleAccount(string email)
    signal emailTo(string address)
    signal newEventWith(string address)
    signal openMessage(string key)
    signal shareByEmail(url file, string name)
    signal manageAccounts()

    // What the middle list shows: all, favourites, group or book.
    property string kind: "all"
    property string value: ""
    property string filterText: ""
    property string sortOrder: "az"
    // all, email or phone.
    property string show: "all"
    property string openKey: ""
    property var current: ({})
    property var recent: []
    property var upcoming: []
    readonly property string currentEmail: current.emails && current.emails.length > 0 ? current.emails[0].value : ""

    readonly property var rows: {
        const all = PeopleEngine.revision >= 0 ? PeopleEngine.list(kind, value, filterText) : [];
        const shown = show === "email" ? all.filter(r => r.hasEmail) : show === "phone" ? all.filter(r => r.hasPhone) : all;
        return sortOrder === "za" ? shown.slice().reverse() : shown;
    }
    readonly property string listTitle: {
        if (kind === "favourites") {
            return qsTr("Favourites");
        }
        if (kind === "group") {
            return value;
        }
        if (kind === "book") {
            const b = PeopleEngine.books.find(x => x.uid === value);
            return b ? b.name : "";
        }
        return qsTr("All contacts");
    }

    function search(text) {
        filterText = text;
    }
    function clearSearch() {
        filterText = "";
    }
    function pick(k, v) {
        kind = k;
        value = v || "";
    }
    function open(key) {
        openKey = key;
        load();
    }
    function load() {
        current = openKey ? PeopleEngine.contact(openKey) : ({});
        if (!current.key) {
            openKey = "";
        }
        recent = [];
        upcoming = [];
        if (currentEmail) {
            MailEngine.findWith(currentEmail, 3);
            CalendarEngine.upcomingWith(currentEmail, 2);
        }
    }
    function birthdayText(b) {
        if (!b) {
            return "";
        }
        if (b.startsWith("--")) {
            const parts = b.substring(2).split("-");
            return Formats.dayAndMonth(new Date(2000, Number(parts[0]) - 1, Number(parts[1])));
        }
        const p = b.split("-");
        return Formats.dayMonthYear(new Date(Number(p[0]), Number(p[1]) - 1, Number(p[2])));
    }
    function emailLabel(type) {
        return type === "HOME" ? qsTr("Email, personal") : type === "WORK" ? qsTr("Email, work") : qsTr("Email");
    }
    function phoneLabel(type) {
        return type === "CELL" ? qsTr("Mobile") : type === "HOME" ? qsTr("Home phone") : type === "WORK" ? qsTr("Work phone") : qsTr("Phone");
    }
    function upcomingText(e) {
        const s = new Date(e.start);
        const today = new Date();
        const t = new Date(today.getFullYear(), today.getMonth(), today.getDate());
        const d = new Date(s.getFullYear(), s.getMonth(), s.getDate());
        const days = Math.round((d - t) / 86400000);
        const day = days === 0 ? qsTr("Today") : days === 1 ? qsTr("Tomorrow") : Formats.weekdayAndDay(s);
        return day + ", " + e.rangeText + (e.location ? "  ·  " + e.location : "");
    }
    function newContact() {
        editor.start({});
    }
    function editCurrent() {
        if (current.key && !current.readOnly) {
            editor.start(current);
        }
    }

    Connections {
        target: PeopleEngine
        function onContactsChanged() {
            if (page.openKey) {
                const was = page.current;
                const now = PeopleEngine.contact(page.openKey);
                if (!now.key) {
                    page.openKey = "";
                    page.current = ({});
                } else if (JSON.stringify(now) !== JSON.stringify(was)) {
                    page.current = now;
                }
            }
        }
        function onSaved(key) {
            if (key !== page.openKey) {
                page.open(key);
            }
        }
        function onNotice(text) {
            toast.show(text);
        }
    }
    Connections {
        target: MailEngine
        function onFoundWith(address, messages) {
            if (address === page.currentEmail) {
                page.recent = messages;
            }
        }
    }
    Connections {
        target: CalendarEngine
        function onUpcomingFound(address, events) {
            if (address === page.currentEmail) {
                page.upcoming = events;
            }
        }
    }

    Shortcut {
        sequence: StandardKey.New
        enabled: page.visible
        onActivated: page.newContact()
    }
    Shortcut {
        sequence: StandardKey.Delete
        enabled: page.visible && page.current.key !== undefined && !page.current.readOnly
        onActivated: deleteConfirm.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: PeopleEngine.problem.length > 0
            Layout.fillWidth: true
            implicitHeight: 40
            color: UI.Style.warningSoft
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: PeopleEngine.problem
                size: 13
            }
        }

        // A Google account Mail has whose contacts aren't here yet.
        Repeater {
            model: PeopleEngine.ready ? page.mailAccounts.filter(a => PeopleEngine.revision >= 0 && PeopleEngine.accountAddresses().indexOf(a.email.toLowerCase()) < 0) : []
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
                        name: "people"
                        size: 16
                        color: UI.Style.iconInk
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Show the contacts, calendar and tasks for %1 here too. Google asks once to allow it.").arg(modelData.email)
                        size: 13
                        elide: Text.ElideRight
                    }
                    UI.Button {
                        size: "small"
                        kind: "primary"
                        text: GoogleSignIn.waiting ? qsTr("Waiting for Google") : qsTr("Show my contacts")
                        busy: GoogleSignIn.waiting
                        enabled: !GoogleSignIn.waiting
                        onClicked: page.addGoogleAccount(modelData.email)
                    }
                }
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

                    MailButton {
                        kind: "primary"
                        size: "small"
                        text: qsTr("New contact")
                        iconName: "user-plus"
                        enabled: PeopleEngine.ready
                        Layout.rightMargin: 6
                        onClicked: page.newContact()
                    }
                    CommandButton {
                        text: qsTr("Edit")
                        iconName: "new-mail"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: page.editCurrent()
                    }
                    CommandButton {
                        text: qsTr("Delete")
                        iconName: "trash"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: deleteConfirm.open()
                    }
                    RibbonDivider {}
                    CommandButton {
                        text: page.current.favourite ? qsTr("Unfavourite") : qsTr("Favourite")
                        iconName: "star"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: PeopleEngine.setFavourite(page.current.key, !page.current.favourite)
                    }
                    CommandButton {
                        id: groupButton
                        text: qsTr("Add to group")
                        iconName: "tag"
                        enabled: page.current.key !== undefined && !page.current.readOnly
                        onClicked: groupMenu.openFor(groupButton)
                    }
                    RibbonDivider {}
                    CommandButton {
                        text: qsTr("Share contact")
                        iconName: "share-out"
                        enabled: page.current.key !== undefined
                        menu: shareMenu
                    }
                    RibbonDivider {}
                    CommandButton {
                        text: qsTr("Import")
                        iconName: "download"
                        enabled: PeopleEngine.ready
                        onClicked: importDialog.open()
                    }
                    CommandButton {
                        text: qsTr("Export")
                        iconName: "upload"
                        enabled: page.rows.length > 0
                        onClicked: exportDialog.open()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    CommandButton {
                        iconOnly: true
                        iconName: "refresh"
                        tip: qsTr("Check for changes")
                        spinning: PeopleEngine.syncing
                        onClicked: PeopleEngine.refresh()
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
                        text: page.sortOrder === "az" ? qsTr("Sort: A to Z") : qsTr("Sort: Z to A")
                        iconName: "sort-down"
                        onClicked: page.sortOrder = page.sortOrder === "az" ? "za" : "az"
                    }
                    CommandButton {
                        text: qsTr("Show")
                        iconName: "filter"
                        menu: showMenu
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
                        text: qsTr("Help with People")
                        iconName: "question"
                        onClicked: Qt.openUrlExternally("https://help.wildos.io/using/people/")
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

                // Everyone, favourites, groups and accounts.
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

                            NavRow {
                                Layout.fillWidth: true
                                iconName: "people"
                                text: qsTr("All contacts")
                                count: PeopleEngine.total
                                current: page.kind === "all"
                                onClicked: page.pick("all")
                            }
                            NavRow {
                                Layout.fillWidth: true
                                iconName: "star"
                                text: qsTr("Favourites")
                                count: PeopleEngine.favourites
                                current: page.kind === "favourites"
                                onClicked: page.pick("favourites")
                            }

                            NavHeading {
                                id: groupsHeading
                                Layout.fillWidth: true
                                text: qsTr("Groups")
                            }
                            Repeater {
                                model: groupsHeading.expanded ? PeopleEngine.groups : []
                                delegate: NavRow {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    iconName: "group"
                                    text: modelData.name
                                    count: modelData.count
                                    current: page.kind === "group" && page.value === modelData.name
                                    onClicked: page.pick("group", modelData.name)
                                }
                            }
                            Item {
                                visible: groupsHeading.expanded
                                Layout.fillWidth: true
                                implicitHeight: 30
                                UI.Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 44
                                    text: qsTr("New group")
                                    size: 13
                                    color: newGroupMouse.containsMouse ? UI.Style.ink : UI.Style.inkMuted
                                }
                                MouseArea {
                                    id: newGroupMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: groupDialog.ask("")
                                }
                            }

                            NavHeading {
                                id: accountsHeading
                                Layout.fillWidth: true
                                text: qsTr("Accounts")
                            }
                            Repeater {
                                model: accountsHeading.expanded ? PeopleEngine.books : []
                                delegate: NavRow {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    iconName: modelData.account ? "mail" : "computer"
                                    text: modelData.name
                                    count: modelData.count
                                    current: page.kind === "book" && page.value === modelData.uid
                                    onClicked: page.pick("book", modelData.uid)
                                }
                            }
                        }
                    }

                    SyncNote {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.topMargin: 8
                        title: PeopleEngine.books.some(b => b.account) ? qsTr("Synced with your accounts") : qsTr("Kept on this PC")
                        detail: qsTr("Stored on this PC, never with us")
                        onClicked: page.manageAccounts()
                    }
                }

                // A to Z.
                Rectangle {
                    Layout.preferredWidth: 340
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
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            Layout.leftMargin: 16
                            Layout.rightMargin: 10
                            spacing: 8
                            UI.Label {
                                text: page.listTitle
                                size: 16
                                font.weight: UI.Style.weightSemiBold
                                elide: Text.ElideRight
                                Layout.maximumWidth: 220
                            }
                            UI.Label {
                                text: page.rows.length
                                size: 13
                                color: UI.Style.inkMuted
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "sort-down"
                                iconColor: UI.Style.iconMuted
                                tip: page.sortOrder === "az" ? qsTr("Sort Z to A") : qsTr("Sort A to Z")
                                onClicked: page.sortOrder = page.sortOrder === "az" ? "za" : "az"
                            }
                            CommandButton {
                                id: filterButton
                                iconOnly: true
                                iconName: "filter"
                                iconColor: page.show === "all" ? UI.Style.iconMuted : UI.Style.iconInk
                                tip: qsTr("Show")
                                onClicked: showMenu.popup(filterButton, 0, filterButton.height + 4)
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            implicitHeight: 1
                            color: UI.Style.borderSubtle
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 2

                            ListView {
                                id: list
                                anchors.fill: parent
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: page.rows
                                QQC.ScrollBar.vertical: UI.ScrollBar {}
                                // Each letter heads the first name under it.
                                delegate: Item {
                                    id: person
                                    required property var modelData
                                    required property int index
                                    readonly property bool current: page.openKey === modelData.key
                                    readonly property bool firstOfLetter: index === 0 || page.rows[index - 1].letter !== modelData.letter
                                    width: ListView.view.width
                                    height: 50 + (firstOfLetter ? 28 : 0)
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: modelData.name

                                    UI.Label {
                                        visible: person.firstOfLetter
                                        x: 16
                                        y: 8
                                        text: person.modelData.letter
                                        size: 12
                                        font.weight: UI.Style.weightSemiBold
                                        color: UI.Style.inkMuted
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.topMargin: person.firstOfLetter ? 28 : 0
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 6
                                        radius: UI.Style.radiusXs
                                        color: person.current ? UI.Style.surfaceSelected : personMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.topMargin: person.firstOfLetter ? 28 : 0
                                        anchors.leftMargin: 16
                                        anchors.rightMargin: 18
                                        spacing: 11
                                        SenderBadge {
                                            initials: person.modelData.initials
                                            colourIndex: person.modelData.colour
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            UI.Label {
                                                Layout.fillWidth: true
                                                text: person.modelData.name
                                                size: 13.5
                                                font.weight: person.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
                                                elide: Text.ElideRight
                                            }
                                            UI.Label {
                                                Layout.fillWidth: true
                                                visible: text.length > 0
                                                text: person.modelData.detail
                                                size: 12
                                                color: UI.Style.inkMuted
                                                elide: Text.ElideRight
                                            }
                                        }
                                        MailIcon {
                                            visible: person.modelData.favourite
                                            name: "star"
                                            size: 14
                                            strokeWidth: 1.8
                                            color: MailIcons.star
                                        }
                                    }
                                    MouseArea {
                                        id: personMouse
                                        anchors.fill: parent
                                        anchors.topMargin: person.firstOfLetter ? 28 : 0
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function (event) {
                                            page.open(person.modelData.key);
                                            if (event.button === Qt.RightButton) {
                                                contactMenu.popup();
                                            }
                                        }
                                        onDoubleClicked: page.editCurrent()
                                    }
                                }
                            }

                            UI.Label {
                                anchors.centerIn: parent
                                width: parent.width - 48
                                visible: list.count === 0 && PeopleEngine.ready
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: page.filterText ? qsTr("Nobody here matches \"%1\".").arg(page.filterText)
                                    : page.kind === "favourites" ? qsTr("Star someone to keep them here.")
                                    : page.kind === "group" ? qsTr("Nobody in this group yet. Choose someone, then Add to group.")
                                    : qsTr("No contacts yet. Add one, import a file, or add an account in Calendar's accounts to bring yours in.")
                                size: 13
                                color: UI.Style.inkMuted
                            }
                            UI.Spinner {
                                anchors.centerIn: parent
                                visible: !PeopleEngine.ready && PeopleEngine.problem.length === 0
                                running: visible
                            }
                        }
                    }
                }

                // The person chosen.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: UI.Style.radiusPanel
                    color: UI.Style.surfaceRest
                    border.width: 1
                    border.color: UI.Style.borderSubtle
                    clip: true

                    UI.Label {
                        anchors.centerIn: parent
                        visible: page.current.key === undefined
                        text: qsTr("Choose someone to see their details")
                        size: 13
                        color: UI.Style.inkMuted
                    }

                    QQC.ScrollView {
                        id: details
                        anchors.fill: parent
                        visible: page.current.key !== undefined
                        clip: true
                        contentWidth: availableWidth
                        QQC.ScrollBar.vertical: UI.ScrollBar {}

                        ColumnLayout {
                            width: details.availableWidth
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 26
                                Layout.rightMargin: 26
                                Layout.topMargin: 22
                                Layout.bottomMargin: 16
                                spacing: 18
                                SenderBadge {
                                    size: 72
                                    initials: page.current.initials || ""
                                    colourIndex: page.current.colour || 0
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    UI.Label {
                                        Layout.fillWidth: true
                                        text: page.current.name || page.currentEmail
                                        size: 24
                                        font.weight: UI.Style.weightSemiBold
                                        elide: Text.ElideRight
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Repeater {
                                            model: page.current.groups || []
                                            delegate: Rectangle {
                                                required property string modelData
                                                implicitWidth: chipLabel.implicitWidth + 18
                                                implicitHeight: 22
                                                radius: 11
                                                color: UI.Style.surfaceControl
                                                UI.Label {
                                                    id: chipLabel
                                                    anchors.centerIn: parent
                                                    text: parent.modelData
                                                    size: 12
                                                    color: UI.Style.inkMuted
                                                }
                                            }
                                        }
                                        UI.Label {
                                            height: 22
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: (page.current.groups || []).length > 0 ? 4 : 0
                                            text: page.current.bookName ? qsTr("Saved in %1").arg(page.current.bookName) : ""
                                            size: 12
                                            color: UI.Style.inkMuted
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.leftMargin: 26
                                Layout.rightMargin: 26
                                Layout.bottomMargin: 16
                                spacing: 8
                                UI.Button {
                                    kind: "primary"
                                    size: "small"
                                    text: qsTr("Email")
                                    iconName: "mail"
                                    enabled: page.currentEmail.length > 0
                                    onClicked: page.emailTo(page.currentEmail)
                                }
                                MailButton {
                                    size: "small"
                                    text: qsTr("New event")
                                    iconName: "calendar"
                                    onClicked: page.newEventWith(page.currentEmail)
                                }
                                UI.Button {
                                    size: "small"
                                    text: qsTr("Edit")
                                    iconName: "pencil"
                                    enabled: !page.current.readOnly
                                    onClicked: page.editCurrent()
                                }
                                UI.IconButton {
                                    iconName: "star"
                                    iconColor: page.current.favourite ? MailIcons.star : UI.Style.iconInk
                                    tip: page.current.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
                                    enabled: !page.current.readOnly
                                    onClicked: PeopleEngine.setFavourite(page.current.key, !page.current.favourite)
                                }
                                UI.IconButton {
                                    id: moreButton
                                    iconName: "more"
                                    tip: qsTr("More")
                                    onClicked: contactMenu.popup(moreButton, 0, moreButton.height + 4)
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 26
                                Layout.rightMargin: 26
                                Layout.bottomMargin: 26
                                columns: details.availableWidth > 640 ? 2 : 1
                                columnSpacing: 14
                                rowSpacing: 14

                                // Contact details.
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    Layout.preferredWidth: 1
                                    implicitHeight: detailColumn.implicitHeight + 24
                                    radius: UI.Style.radiusPanel
                                    color: UI.Style.surfaceRest
                                    border.width: 1
                                    border.color: UI.Style.borderSubtle

                                    ColumnLayout {
                                        id: detailColumn
                                        x: 16
                                        y: 12
                                        width: parent.width - 32
                                        spacing: 0
                                        UI.Label {
                                            text: qsTr("Contact details")
                                            size: 13
                                            font.weight: UI.Style.weightSemiBold
                                            Layout.bottomMargin: 4
                                        }
                                        Repeater {
                                            model: {
                                                const c = page.current;
                                                const items = [];
                                                for (const e of (c.emails || [])) {
                                                    items.push({ icon: "mail", label: page.emailLabel(e.type), value: e.value, link: "mailto:" + e.value });
                                                }
                                                for (const p of (c.phones || [])) {
                                                    items.push({ icon: "phone", label: page.phoneLabel(p.type), value: p.value, link: "" });
                                                }
                                                if (c.company) {
                                                    items.push({ icon: "home", label: qsTr("Company"), value: c.company, link: "" });
                                                }
                                                if (c.birthday) {
                                                    items.push({ icon: "cake", label: qsTr("Birthday"), value: page.birthdayText(c.birthday), link: "" });
                                                }
                                                if (c.notes) {
                                                    items.push({ icon: "notes", label: qsTr("Notes"), value: c.notes, link: "" });
                                                }
                                                return items;
                                            }
                                            delegate: RowLayout {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                Layout.topMargin: 9
                                                Layout.bottomMargin: 9
                                                spacing: 12
                                                MailIcon {
                                                    Layout.alignment: Qt.AlignTop
                                                    Layout.topMargin: 2
                                                    name: modelData.icon
                                                    size: 17
                                                    strokeWidth: 1.8
                                                    color: UI.Style.iconMuted
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 2
                                                    UI.Label {
                                                        text: modelData.label
                                                        size: 12
                                                        color: UI.Style.inkMuted
                                                    }
                                                    UI.Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.value
                                                        size: 13.5
                                                        wrapMode: Text.Wrap
                                                        lineHeight: 1.15
                                                        MouseArea {
                                                            anchors.fill: parent
                                                            enabled: modelData.link.length > 0
                                                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                            onClicked: page.emailTo(modelData.value)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        UI.Label {
                                            visible: !(page.current.emails || []).length && !(page.current.phones || []).length && !page.current.birthday && !page.current.notes
                                            Layout.topMargin: 9
                                            text: qsTr("Nothing saved yet. Edit to add an address or number.")
                                            size: 13
                                            color: UI.Style.inkMuted
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    Layout.preferredWidth: 1
                                    spacing: 14

                                    // Recent email.
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: recentColumn.implicitHeight + 24
                                        radius: UI.Style.radiusPanel
                                        color: UI.Style.surfaceRest
                                        border.width: 1
                                        border.color: UI.Style.borderSubtle

                                        ColumnLayout {
                                            id: recentColumn
                                            x: 16
                                            y: 12
                                            width: parent.width - 32
                                            spacing: 0
                                            UI.Label {
                                                text: qsTr("Recent email")
                                                size: 13
                                                font.weight: UI.Style.weightSemiBold
                                                Layout.bottomMargin: 4
                                            }
                                            Repeater {
                                                model: page.recent
                                                delegate: Item {
                                                    id: mail
                                                    required property var modelData
                                                    required property int index
                                                    Layout.fillWidth: true
                                                    implicitHeight: mailColumn.implicitHeight + 16
                                                    Rectangle {
                                                        visible: mail.index > 0
                                                        width: parent.width
                                                        height: 1
                                                        color: UI.Style.borderSubtle
                                                    }
                                                    ColumnLayout {
                                                        id: mailColumn
                                                        y: 8
                                                        width: parent.width
                                                        spacing: 1
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            spacing: 8
                                                            UI.Label {
                                                                Layout.fillWidth: true
                                                                text: mail.modelData.subject
                                                                size: 13
                                                                font.weight: mail.modelData.unread ? UI.Style.weightSemiBold : UI.Style.weightRegular
                                                                elide: Text.ElideRight
                                                            }
                                                            UI.Label {
                                                                text: mail.modelData.time
                                                                size: 12
                                                                color: UI.Style.inkMuted
                                                            }
                                                        }
                                                        UI.Label {
                                                            Layout.fillWidth: true
                                                            visible: text.length > 0
                                                            text: mail.modelData.preview
                                                            size: 12
                                                            color: UI.Style.inkMuted
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: page.openMessage(mail.modelData.key)
                                                    }
                                                }
                                            }
                                            UI.Label {
                                                visible: page.recent.length === 0
                                                Layout.topMargin: 8
                                                Layout.bottomMargin: 4
                                                text: page.currentEmail ? qsTr("No mail with them on this PC yet.") : qsTr("No email address saved.")
                                                size: 12
                                                color: UI.Style.inkMuted
                                            }
                                        }
                                    }

                                    // Coming up.
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: upcomingColumn.implicitHeight + 24
                                        radius: UI.Style.radiusPanel
                                        color: UI.Style.surfaceRest
                                        border.width: 1
                                        border.color: UI.Style.borderSubtle

                                        ColumnLayout {
                                            id: upcomingColumn
                                            x: 16
                                            y: 12
                                            width: parent.width - 32
                                            spacing: 0
                                            UI.Label {
                                                text: qsTr("Coming up")
                                                size: 13
                                                font.weight: UI.Style.weightSemiBold
                                                Layout.bottomMargin: 4
                                            }
                                            Repeater {
                                                model: page.upcoming
                                                delegate: RowLayout {
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    Layout.topMargin: 8
                                                    Layout.bottomMargin: 8
                                                    spacing: 12
                                                    Rectangle {
                                                        implicitWidth: 4
                                                        implicitHeight: 36
                                                        radius: 2
                                                        color: modelData.color
                                                    }
                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2
                                                        UI.Label {
                                                            Layout.fillWidth: true
                                                            text: modelData.title
                                                            size: 13
                                                            font.weight: UI.Style.weightSemiBold
                                                            elide: Text.ElideRight
                                                        }
                                                        UI.Label {
                                                            Layout.fillWidth: true
                                                            text: page.upcomingText(modelData)
                                                            size: 12
                                                            color: UI.Style.inkMuted
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                }
                                            }
                                            UI.Label {
                                                visible: page.upcoming.length === 0
                                                Layout.topMargin: 8
                                                Layout.bottomMargin: 4
                                                text: qsTr("Nothing in your calendar with them.")
                                                size: 12
                                                color: UI.Style.inkMuted
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Menu {
        id: showMenu
        entries: [
            { text: qsTr("Everyone"), radio: true, checked: page.show === "all", run: () => page.show = "all" },
            { text: qsTr("With an email address"), radio: true, checked: page.show === "email", run: () => page.show = "email" },
            { text: qsTr("With a phone number"), radio: true, checked: page.show === "phone", run: () => page.show = "phone" }
        ]
    }

    UI.Menu {
        id: shareMenu
        entries: [
            { text: qsTr("Send by email"), icon: "send", run: () => {
                const file = PeopleEngine.shareFile(page.current.key);
                if (file.toString().length > 0) {
                    page.shareByEmail(file, page.current.name);
                }
            } },
            { text: qsTr("Save as a contact card"), icon: "download", run: () => saveCardDialog.open() }
        ]
    }

    UI.Menu {
        id: groupMenu
        function openFor(item) {
            const groups = PeopleEngine.groups.map(g => g.name);
            const mine = page.current.groups || [];
            entries = groups.map(g => ({
                text: g,
                checked: mine.indexOf(g) >= 0,
                run: () => mine.indexOf(g) >= 0 ? PeopleEngine.removeFromGroup(page.current.key, g) : PeopleEngine.addToGroup(page.current.key, g)
            })).concat(groups.length > 0 ? [{ separator: true }] : []).concat([
                { text: qsTr("New group"), icon: "plus", run: () => groupDialog.ask(page.current.key) }
            ]);
            if (item) {
                popup(item, 0, item.height + 4);
            } else {
                popup();
            }
        }
    }

    UI.Menu {
        id: contactMenu
        entries: [
            { text: qsTr("Email"), icon: "mail", enabled: page.currentEmail.length > 0, run: () => page.emailTo(page.currentEmail) },
            { text: qsTr("New event"), run: () => page.newEventWith(page.currentEmail) },
            { separator: true },
            { text: qsTr("Edit"), icon: "pencil", enabled: !page.current.readOnly, run: () => page.editCurrent() },
            { text: page.current.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites"), icon: "star", enabled: !page.current.readOnly, run: () => PeopleEngine.setFavourite(page.current.key, !page.current.favourite) },
            { text: qsTr("Add to group"), enabled: !page.current.readOnly, run: () => groupMenu.openFor(null) },
            { text: qsTr("Share contact"), icon: "share", submenu: shareMenu.entries },
            { separator: true },
            { text: qsTr("Delete"), icon: "delete", danger: true, enabled: !page.current.readOnly, run: () => deleteConfirm.open() }
        ]
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import contacts")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Contact cards (*.vcf *.vcard)"), qsTr("All files (*)")]
        onAccepted: PeopleEngine.importFrom(selectedFile, page.kind === "book" ? page.value : "")
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export contacts")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "vcf"
        nameFilters: [qsTr("Contact cards (*.vcf)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/" + page.listTitle + ".vcf"
        onAccepted: PeopleEngine.exportTo(page.rows.map(r => r.key), selectedFile)
    }
    FileDialog {
        id: saveCardDialog
        title: qsTr("Save contact card")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "vcf"
        nameFilters: [qsTr("Contact cards (*.vcf)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/" + (page.current.name || qsTr("Contact")) + ".vcf"
        onAccepted: PeopleEngine.exportTo([page.current.key], selectedFile)
    }

    UI.Dialog {
        id: deleteConfirm
        title: qsTr("Delete %1?").arg(page.current.name || "")
        text: page.current.bookName ? qsTr("They're deleted from %1 too, and from every device that syncs with it.").arg(page.current.bookName) : qsTr("This can't be undone.")
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
                    PeopleEngine.removeContacts([page.current.key]);
                    page.openKey = "";
                    page.current = ({});
                    deleteConfirm.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: groupDialog
        property string contactKey
        function ask(key) {
            contactKey = key;
            groupName.text = "";
            open();
            groupName.forceActiveFocus();
        }
        title: qsTr("New group")
        text: contactKey ? qsTr("%1 goes in it straight away.").arg(page.current.name || "") : ""
        UI.TextField {
            id: groupName
            Layout.fillWidth: true
            placeholderText: qsTr("Group name")
            onAccepted: groupCreate.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: groupDialog.close()
            },
            UI.Button {
                id: groupCreate
                kind: "primary"
                text: qsTr("Create")
                enabled: groupName.text.trim().length > 0
                onClicked: {
                    const name = groupName.text.trim();
                    if (groupDialog.contactKey) {
                        PeopleEngine.addToGroup(groupDialog.contactKey, name);
                    } else {
                        PeopleEngine.addGroup(name);
                    }
                    groupDialog.close();
                }
            }
        ]
    }

    ContactEditor {
        id: editor
        books: PeopleEngine.books.filter(b => !b.readOnly)
        defaultBook: page.kind === "book" ? page.value : PeopleEngine.defaultBook
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 60
    }
}
