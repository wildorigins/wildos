import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// Settings, Automatic replies (MailMsSetAutoReply.dc): per account, on
// until turned off or between two dates, the subject and message, and who
// gets one. A Microsoft account's replies are set on Microsoft's server and
// go out when this PC is off; any other account's are sent by Mail while
// this PC is on.
ColumnLayout {
    id: page

    signal signIn(string accountId)

    property string account: MailEngine.accounts.length > 0 ? MailEngine.accounts[0].id : ""
    property var reply: ({})
    property bool loading: false
    property bool saving: false
    property string note: ""
    property bool timed: false
    property date start: new Date()
    property date end: new Date()
    property string audience: "all"

    spacing: 14

    function load() {
        if (account.length === 0) {
            return;
        }
        loading = true;
        note = "";
        MailEngine.loadAutoReply(account);
    }
    onAccountChanged: if (visible) load()
    onVisibleChanged: if (visible) load()

    function save() {
        saving = true;
        note = "";
        MailEngine.saveAutoReply(account, {
            enabled: onSwitch.checked,
            subject: subjectField.text,
            message: messageText.text,
            audience: audience,
            start: timed ? start : null,
            end: timed ? end : null
        });
    }

    // What the status line under the account says.
    readonly property string status: {
        if (!onSwitch.checked) {
            return qsTr("Off");
        }
        if (!timed) {
            return qsTr("On until you turn it off");
        }
        const now = new Date();
        if (now < start) {
            return qsTr("Starts %1").arg(Formats.longDate(start));
        }
        if (now < end) {
            return qsTr("On until %1").arg(Formats.longDate(end));
        }
        return qsTr("Ended %1").arg(Formats.longDate(end));
    }

    Connections {
        target: MailEngine
        function onAutoReplyLoaded(accountId, settings) {
            if (accountId !== page.account) {
                return;
            }
            page.loading = false;
            page.reply = settings;
            onSwitch.checked = settings.enabled === true;
            subjectField.text = settings.subject || "";
            messageText.text = settings.message || "";
            page.audience = settings.audience || "all";
            const s = settings.start ? new Date(settings.start) : null;
            const e = settings.end ? new Date(settings.end) : null;
            page.timed = !!(s && e && !isNaN(s.getTime()) && !isNaN(e.getTime()));
            const now = new Date();
            page.start = page.timed ? s : new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1, 9, 0);
            page.end = page.timed ? e : new Date(now.getFullYear(), now.getMonth(), now.getDate() + 8, 9, 0);
            page.note = settings.error || "";
        }
        function onAutoReplySaved(accountId, ok, message, needsSignIn) {
            if (accountId !== page.account) {
                return;
            }
            page.saving = false;
            page.reply = Object.assign({}, page.reply, { needsSignIn: needsSignIn });
            page.note = ok ? qsTr("Saved.") : message;
        }
    }

    // The account, and how its replies stand.
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        UI.Label {
            text: qsTr("Account")
            size: 13
        }
        UI.ComboBox {
            Layout.preferredWidth: 280
            enabled: MailEngine.accounts.length > 1
            model: MailEngine.accounts.map(a => ({ value: a.id, text: a.email }))
            currentIndex: MailEngine.accounts.findIndex(a => a.id === page.account)
            onActivated: function (i) {
                page.account = MailEngine.accounts[i].id;
            }
        }
        Item {
            Layout.fillWidth: true
        }
        UI.Spinner {
            visible: page.loading
            size: 16
        }
        UI.Label {
            visible: !page.loading
            text: page.status
            size: 13
            color: onSwitch.checked ? UI.Style.accentText : UI.Style.inkMuted
        }
    }

    // When.
    UI.Card {
        Layout.fillWidth: true
        padding: 16
        enabled: !page.loading && page.reply.needsSignIn !== true

        ColumnLayout {
            width: parent.width
            spacing: 12
            UI.Label {
                text: qsTr("When")
                size: 14
                font.weight: UI.Style.weightSemiBold
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Send automatic replies")
                description: qsTr("Each person gets the reply once, however many times they write")
                UI.Toggle {
                    id: onSwitch
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                enabled: onSwitch.checked
                title: qsTr("Schedule")
                RowLayout {
                    spacing: 6
                    UI.SegmentButton {
                        text: qsTr("Until I turn it off")
                        checked: !page.timed
                        onClicked: page.timed = false
                    }
                    UI.SegmentButton {
                        text: qsTr("Between two dates")
                        checked: page.timed
                        onClicked: page.timed = true
                    }
                }
            }
            RowLayout {
                visible: page.timed && onSwitch.checked
                spacing: 8
                UI.Label {
                    text: qsTr("Between", "dates")
                    size: 13
                }
                UI.Button {
                    size: "small"
                    text: Formats.mediumDate(page.start)
                    onClicked: {
                        picker.apply = when => page.start = when;
                        picker.ask(qsTr("Start"), page.start, true);
                    }
                }
                UI.Button {
                    size: "small"
                    text: Formats.time(page.start)
                    onClicked: {
                        picker.apply = when => page.start = when;
                        picker.ask(qsTr("Start"), page.start, true);
                    }
                }
                UI.Label {
                    text: qsTr("and", "dates")
                    size: 13
                }
                UI.Button {
                    size: "small"
                    text: Formats.mediumDate(page.end)
                    onClicked: {
                        picker.apply = when => page.end = when;
                        picker.ask(qsTr("End"), page.end, true);
                    }
                }
                UI.Button {
                    size: "small"
                    text: Formats.time(page.end)
                    onClicked: {
                        picker.apply = when => page.end = when;
                        picker.ask(qsTr("End"), page.end, true);
                    }
                }
            }
            // Microsoft's replies take the subject they answer; only Mail's
            // own have one of their own.
            UI.SettingRow {
                Layout.fillWidth: true
                visible: page.reply.server !== true
                enabled: onSwitch.checked
                title: qsTr("Subject")
                UI.TextField {
                    id: subjectField
                    width: 320
                    placeholderText: qsTr("Automatic reply")
                }
            }
            QQC.TextArea {
                id: messageText
                Layout.fillWidth: true
                Layout.preferredHeight: 130
                enabled: onSwitch.checked
                wrapMode: TextEdit.Wrap
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.pixelSize: UI.Style.px(13)
                placeholderText: qsTr("For example: I'm away until Monday and will reply when I'm back.")
                placeholderTextColor: UI.Style.inkPlaceholder
                background: Rectangle {
                    radius: UI.Style.radiusXs
                    color: UI.Style.surfaceSunken
                    border.width: 1
                    border.color: messageText.activeFocus ? UI.Style.accent : UI.Style.borderField
                }
            }
        }
    }

    // Who gets one.
    UI.Card {
        Layout.fillWidth: true
        padding: 16
        enabled: !page.loading && page.reply.needsSignIn !== true && onSwitch.checked

        ColumnLayout {
            width: parent.width
            spacing: 12
            UI.Label {
                text: qsTr("Who gets one")
                size: 14
                font.weight: UI.Style.weightSemiBold
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Reply to")
                description: qsTr("Newsletters, receipts and no-reply addresses never get one")
                UI.ComboBox {
                    readonly property var choices: page.reply.server === true && page.reply.personal !== true
                        ? [{ value: "all", text: qsTr("Everyone") }, { value: "contacts", text: qsTr("My contacts and my organisation") }, { value: "organisation", text: qsTr("Only my organisation") }]
                        : [{ value: "all", text: qsTr("Everyone") }, { value: "contacts", text: qsTr("Only my contacts") }]
                    model: choices
                    currentIndex: Math.max(0, choices.findIndex(c => c.value === page.audience))
                    onActivated: function (i) {
                        page.audience = choices[i].value;
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        UI.Button {
            visible: page.reply.needsSignIn === true
            kind: "primary"
            text: qsTr("Sign in again")
            onClicked: page.signIn(page.account)
        }
        UI.Button {
            visible: page.reply.needsSignIn !== true
            kind: "primary"
            text: qsTr("Save")
            busy: page.saving
            enabled: !page.loading && !page.saving && (!onSwitch.checked || messageText.text.trim().length > 0)
                && (!page.timed || page.end > page.start)
            onClicked: page.save()
        }
        UI.Label {
            Layout.fillWidth: true
            text: page.note
            size: 12
            color: page.note === qsTr("Saved.") ? UI.Style.inkMuted : UI.Style.warning
            wrapMode: Text.WordWrap
        }
    }

    UI.Label {
        Layout.fillWidth: true
        text: page.reply.server === true ? qsTr("Microsoft sends these from its own servers, so they go out when this PC is off.")
                                         : qsTr("Mail sends these itself, so they go out while this PC is on and Mail can reach your email provider.")
        size: 12
        color: UI.Style.inkMuted
        wrapMode: Text.WordWrap
    }

    DueDialog {
        id: picker
        needsTime: true
        property var apply: null
        onChosen: function (when) {
            if (apply) {
                apply(when);
            }
        }
    }
}
