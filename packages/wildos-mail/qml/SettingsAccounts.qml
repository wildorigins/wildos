import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Settings, Accounts (MailMsSetAccounts.dc, MailMsSetAccountsDrag.dc): Add
// account and Import mail first, then the accounts in their order, which is
// the order of the folder list, the From menu and search results. An
// account opens to its settings; accounts move by dragging, with Alt and the
// arrow keys, or from the right-click menu.
ColumnLayout {
    id: accounts

    signal addAccount()
    signal importMail()
    signal signIn(string accountId)
    signal removeAccount(var account)

    // The account whose settings are open; the default one to begin with.
    property string expanded: MailEngine.defaultAccount
    // The account being dragged, and where it would land.
    property string dragging: ""

    spacing: 14

    function providerName(a) {
        const names = {
            gmail: "Gmail",
            outlook: a.auth === "microsoft" && !/@(outlook|hotmail|live|msn)\./i.test(a.email) ? qsTr("Microsoft 365") : qsTr("Outlook"),
            icloud: qsTr("iCloud Mail"),
            yahoo: qsTr("Yahoo Mail"),
            fastmail: "Fastmail",
            aol: qsTr("AOL Mail")
        };
        return names[a.provider] || qsTr("Other account (IMAP)");
    }
    function move(id, to) {
        const count = MailEngine.accounts.length;
        to = Math.max(0, Math.min(count - 1, to));
        if (to !== MailEngine.accountIndex(id)) {
            MailEngine.moveAccount(id, to);
        }
    }

    // The order on screen while dragging, so the others slide out of the
    // way before the move is made.
    ListModel {
        id: order
    }
    function syncOrder() {
        order.clear();
        for (const a of MailEngine.accounts) {
            order.append({ accountId: a.id });
        }
    }
    Connections {
        target: MailEngine
        function onAccountsChanged() {
            if (accounts.dragging.length === 0) {
                accounts.syncOrder();
            }
        }
    }
    Component.onCompleted: syncOrder()

    RowLayout {
        spacing: 8
        UI.Button {
            text: qsTr("Add account")
            iconName: "plus"
            kind: "primary"
            onClicked: accounts.addAccount()
        }
        UI.Button {
            text: qsTr("Import mail")
            iconName: "download"
            enabled: MailEngine.accounts.length > 0
            onClicked: accounts.importMail()
        }
    }

    UI.Label {
        visible: MailEngine.accounts.length > 1
        text: accounts.dragging.length > 0 ? qsTr("Drop to place it %1").arg([qsTr("first"), qsTr("second"), qsTr("third"), qsTr("fourth"), qsTr("fifth")][list.dropIndex] || (list.dropIndex + 1))
                                           : qsTr("Drag to change the order")
        size: 12
        color: UI.Style.inkMuted
    }

    ListView {
        id: list
        Layout.fillWidth: true
        implicitHeight: contentHeight
        interactive: false
        spacing: 8
        model: order
        property int dropIndex: 0

        move: Transition {
            NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutBack; easing.overshoot: 0.6 }
        }
        displaced: Transition {
            NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutBack; easing.overshoot: 0.6 }
        }

        delegate: Rectangle {
            id: row
            required property string accountId
            required property int index
            readonly property var account: MailEngine.accounts.find(a => a.id === accountId) || ({})
            readonly property bool open: accounts.expanded === accountId
            readonly property bool lifted: accounts.dragging === accountId
            readonly property var parts: open ? CalendarEngine.accountParts(account.email || "") : ({})

            width: list.width
            height: body.implicitHeight + 28
            radius: UI.Style.radiusMd
            color: UI.Style.surfaceRaised
            border.width: activeFocus ? 2 : 1
            border.color: activeFocus ? UI.Style.accent : UI.Style.borderSubtle
            z: lifted ? 10 : 0
            scale: lifted && !UI.Style.reducedMotion ? 1.01 : 1
            Behavior on scale {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
            activeFocusOnTab: true
            Accessible.role: Accessible.ListItem
            Accessible.name: (account.email || "") + (account.isDefault ? ", " + qsTr("Default") : "")

            // The lift's shadow.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                z: -1
                radius: parent.radius + 1
                color: "transparent"
                border.width: row.lifted ? 6 : 0
                border.color: Qt.rgba(0, 0, 0, 0.18)
                opacity: row.lifted ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: 120 }
                }
            }

            Keys.onPressed: function (event) {
                if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Up) {
                    accounts.move(accountId, index - 1);
                    event.accepted = true;
                } else if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Down) {
                    accounts.move(accountId, index + 1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    accounts.expanded = row.open ? "" : accountId;
                    event.accepted = true;
                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    rowMenu.accountId = accountId;
                    rowMenu.popup(row, 16, 48);
                    event.accepted = true;
                }
            }

            ColumnLayout {
                id: body
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    // The handle: drag it to move the account.
                    Item {
                        implicitWidth: 16
                        implicitHeight: 36
                        visible: MailEngine.accounts.length > 1
                        Accessible.name: qsTr("Drag to re-order")
                        MailIcon {
                            anchors.centerIn: parent
                            name: "grip"
                            size: 14
                            color: UI.Style.iconMuted
                        }
                        MouseArea {
                            id: handle
                            anchors.fill: parent
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            preventStealing: true
                            property real startY: 0
                            onPressed: function (mouse) {
                                startY = mapToItem(list, mouse.x, mouse.y).y;
                                accounts.dragging = row.accountId;
                                list.dropIndex = row.index;
                                row.forceActiveFocus();
                            }
                            onPositionChanged: function (mouse) {
                                const y = mapToItem(list, mouse.x, mouse.y).y;
                                const target = list.indexAt(10, y);
                                if (target >= 0 && target !== row.index) {
                                    order.move(row.index, target, 1);
                                    list.dropIndex = target;
                                }
                            }
                            onReleased: {
                                const id = accounts.dragging;
                                const to = list.dropIndex;
                                accounts.dragging = "";
                                accounts.move(id, to);
                                accounts.syncOrder();
                            }
                            onCanceled: {
                                accounts.dragging = "";
                                accounts.syncOrder();
                            }
                        }
                    }

                    SenderBadge {
                        size: 36
                        initials: MailEngine.initialsOf(row.account.fullName || row.account.email || "")
                        colourIndex: MailEngine.colourOf(row.account.email || "")
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        RowLayout {
                            spacing: 8
                            UI.Label {
                                text: row.account.email || ""
                                size: 14
                                font.weight: UI.Style.weightSemiBold
                                elide: Text.ElideRight
                            }
                            UI.Chip {
                                visible: row.account.isDefault === true
                                text: qsTr("Default")
                            }
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 · %2").arg(accounts.providerName(row.account))
                                .arg(row.account.auth === "google" || row.account.auth === "microsoft" ? qsTr("Mail, calendar and contacts") : qsTr("Mail"))
                            size: 12
                            color: UI.Style.inkMuted
                            elide: Text.ElideRight
                        }
                    }
                    UI.Label {
                        text: row.account.problem ? row.account.problem
                            : row.account.connected ? qsTr("Up to date") : MailEngine.online ? qsTr("Connecting") : qsTr("Offline")
                        color: row.account.problem ? UI.Style.warning : UI.Style.inkMuted
                        size: 12
                        Layout.maximumWidth: 220
                        elide: Text.ElideRight
                    }
                    UI.IconButton {
                        iconName: row.open ? "chevron-up" : "chevron-down"
                        tip: row.open ? qsTr("Close") : qsTr("Settings for this account")
                        onClicked: accounts.expanded = row.open ? "" : row.accountId
                    }
                }

                // Opens and closes on the header too.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        rowMenu.accountId = row.accountId;
                        rowMenu.popup();
                    }
                }

                GridLayout {
                    visible: row.open
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 20
                    rowSpacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 6
                        UI.Label {
                            text: qsTr("Name people see")
                            size: 12
                            color: UI.Style.inkMuted
                        }
                        UI.TextField {
                            Layout.fillWidth: true
                            text: row.account.fullName || ""
                            function save() {
                                const a = MailEngine.account(row.accountId);
                                if (a.fullName !== text.trim()) {
                                    a.fullName = text.trim();
                                    MailEngine.updateAccount(a);
                                }
                            }
                            onEditingFinished: if (!textMenuOpen) save()
                            onTextMenuFocusLeft: save()
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 6
                        UI.Label {
                            text: qsTr("Keep mail on this PC from")
                            size: 12
                            color: UI.Style.inkMuted
                        }
                        UI.ComboBox {
                            id: keepBox
                            Layout.fillWidth: true
                            readonly property var choices: [
                                { value: 0, text: qsTr("All time") },
                                { value: 12, text: qsTr("The last year") },
                                { value: 3, text: qsTr("The last 3 months") },
                                { value: 1, text: qsTr("The last month") },
                                { value: -1, text: qsTr("Only what I open") }
                            ]
                            model: choices
                            currentIndex: choices.findIndex(c => c.value === (row.account.keepAll === false ? -1 : (row.account.keepMonths || 0)))
                            onActivated: function (i) {
                                const a = MailEngine.account(row.accountId);
                                a.keepAll = choices[i].value !== -1;
                                a.keepMonths = Math.max(0, choices[i].value);
                                MailEngine.updateAccount(a);
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 6
                        UI.Label {
                            text: qsTr("Check for new mail")
                            size: 12
                            color: UI.Style.inkMuted
                        }
                        UI.ComboBox {
                            Layout.fillWidth: true
                            readonly property var choices: [
                                { value: 0, text: qsTr("As it arrives") },
                                { value: 15, text: qsTr("Every 15 minutes") },
                                { value: 60, text: qsTr("Every hour") },
                                { value: -1, text: qsTr("Only when I check") }
                            ]
                            model: choices
                            currentIndex: choices.findIndex(c => c.value === (row.account.checkEvery || 0))
                            onActivated: function (i) {
                                const a = MailEngine.account(row.accountId);
                                a.checkEvery = choices[i].value;
                                MailEngine.updateAccount(a);
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 6
                        UI.Label {
                            text: qsTr("Sync")
                            size: 12
                            color: UI.Style.inkMuted
                        }
                        // Each on or off by itself. Mail is always on: it is a
                        // mail account. Calendar and contacts come with the
                        // accounts that have them (Google, Microsoft 365).
                        RowLayout {
                            spacing: 14
                            UI.CheckBox {
                                text: qsTr("Mail")
                                checked: true
                                enabled: false
                            }
                            UI.CheckBox {
                                text: qsTr("Calendar")
                                enabled: row.parts.exists === true
                                checked: row.parts.calendar === true
                                onToggled: CalendarEngine.setAccountPart(row.account.email, "calendar", checked)
                            }
                            UI.CheckBox {
                                text: qsTr("Contacts")
                                enabled: row.parts.exists === true
                                checked: row.parts.contacts === true
                                onToggled: CalendarEngine.setAccountPart(row.account.email, "contacts", checked)
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.columnSpan: 2
                        spacing: 6
                        UI.Label {
                            text: qsTr("Account name in Mail")
                            size: 12
                            color: UI.Style.inkMuted
                        }
                        UI.TextField {
                            Layout.preferredWidth: 300
                            text: row.account.name || ""
                            function save() {
                                const a = MailEngine.account(row.accountId);
                                const name = text.trim() || a.email;
                                if (a.name !== name) {
                                    a.name = name;
                                    MailEngine.updateAccount(a);
                                }
                            }
                            onEditingFinished: if (!textMenuOpen) save()
                            onTextMenuFocusLeft: save()
                        }
                    }
                }

                RowLayout {
                    visible: row.open
                    Layout.fillWidth: true
                    spacing: 10
                    UI.Toggle {
                        checked: row.account.isDefault === true
                        accessibleName: qsTr("Send new mail from this account")
                        onToggled: {
                            if (checked) {
                                MailEngine.defaultAccount = row.accountId;
                            } else {
                                // One account always sends new mail.
                                checked = true;
                            }
                        }
                    }
                    UI.Label {
                        text: qsTr("Send new mail from this account")
                        size: 13
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    UI.Button {
                        text: qsTr("Sign in again")
                        size: "small"
                        iconName: "refresh"
                        onClicked: accounts.signIn(row.accountId)
                    }
                    UI.Button {
                        text: qsTr("Remove account")
                        size: "small"
                        danger: true
                        onClicked: accounts.removeAccount(row.account)
                    }
                }
            }

            TapHandler {
                enabled: !row.open
                onTapped: accounts.expanded = row.accountId
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: MailEngine.accounts.length > 1
        text: qsTr("The order here is the order of your accounts in the folder list, the From menu and search results.")
        size: 12
        color: UI.Style.inkMuted
        wrapMode: Text.WordWrap
    }

    UI.Menu {
        id: rowMenu
        property string accountId
        entries: {
            const at = MailEngine.accounts.findIndex(a => a.id === rowMenu.accountId);
            const last = MailEngine.accounts.length - 1;
            return [
                { text: qsTr("Move up"), icon: "chevron-up", enabled: at > 0, run: () => accounts.move(rowMenu.accountId, at - 1) },
                { text: qsTr("Move down"), icon: "chevron-down", enabled: at >= 0 && at < last, run: () => accounts.move(rowMenu.accountId, at + 1) },
                { text: qsTr("Move to top"), enabled: at > 0, run: () => accounts.move(rowMenu.accountId, 0) }
            ];
        }
    }
}
