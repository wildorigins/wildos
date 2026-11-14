import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// Adding or changing a contact: name, addresses, numbers, company,
// birthday and notes, and for a new one which account keeps it.
UI.Dialog {
    id: editor

    // Address books that can take a new contact, and the one to offer first.
    property var books: []
    property string defaultBook: ""

    property string key: ""
    property var emails: []
    property var phones: []
    // Typing into a row changes its entry in place; this tells bindings.
    property int edits: 0

    readonly property var emailTypes: [
        { value: "HOME", text: qsTr("Personal") },
        { value: "WORK", text: qsTr("Work") },
        { value: "OTHER", text: qsTr("Other") }
    ]
    readonly property var phoneTypes: [
        { value: "CELL", text: qsTr("Mobile") },
        { value: "HOME", text: qsTr("Home") },
        { value: "WORK", text: qsTr("Work") },
        { value: "", text: qsTr("Other") }
    ]
    readonly property var days: [{ value: 0, text: qsTr("Day") }].concat(Array.from({ length: 31 }, (_, i) => ({ value: i + 1, text: String(i + 1) })))
    readonly property var months: [{ value: 0, text: qsTr("Month") }].concat(Array.from({ length: 12 }, (_, i) => ({ value: i + 1, text: Formats.names().monthName(i, Locale.LongFormat) })))

    function typeIndex(types, value) {
        const i = types.findIndex(t => t.value === value);
        return i >= 0 ? i : types.length - 1;
    }

    function start(contact) {
        key = contact.key || "";
        nameField.text = contact.name || "";
        companyField.text = contact.company || "";
        notesField.text = contact.notes || "";
        emails = (contact.emails && contact.emails.length > 0 ? contact.emails : [{ type: "HOME", value: "" }]).map(e => ({ type: e.type || "HOME", value: e.value }));
        phones = (contact.phones && contact.phones.length > 0 ? contact.phones : [{ type: "CELL", value: "" }]).map(p => ({ type: p.type || "", value: p.value }));
        const b = contact.birthday || "";
        let y = "", m = 0, d = 0;
        if (b.startsWith("--")) {
            const parts = b.substring(2).split("-");
            m = Number(parts[0]);
            d = Number(parts[1]);
        } else if (b.length >= 10) {
            const parts = b.split("-");
            y = parts[0];
            m = Number(parts[1]);
            d = Number(parts[2]);
        }
        dayBox.currentIndex = d;
        monthBox.currentIndex = m;
        yearField.text = y;
        const bookIndex = books.findIndex(x => x.uid === defaultBook);
        bookBox.currentIndex = bookIndex >= 0 ? bookIndex : 0;
        open();
        nameField.forceActiveFocus();
    }

    function birthday() {
        const d = dayBox.currentIndex;
        const m = monthBox.currentIndex;
        if (d === 0 || m === 0) {
            return "";
        }
        const pad = n => (n < 10 ? "0" : "") + n;
        const y = yearField.text.trim();
        return /^\d{4}$/.test(y) ? y + "-" + pad(m) + "-" + pad(d) : "--" + pad(m) + "-" + pad(d);
    }

    function save() {
        const data = {
            name: nameField.text.trim(),
            company: companyField.text.trim(),
            notes: notesField.text,
            emails: emails.filter(e => e.value.trim().length > 0),
            phones: phones.filter(p => p.value.trim().length > 0),
            birthday: birthday()
        };
        if (key) {
            data.key = key;
        } else if (books.length > 0) {
            data.bookUid = books[bookBox.currentIndex].uid;
        }
        PeopleEngine.saveContact(data);
        close();
    }

    title: key ? qsTr("Edit contact") : qsTr("New contact")

    QQC.ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(form.implicitHeight, editor.parent ? editor.parent.height - 220 : 480)
        contentWidth: availableWidth
        clip: true
        QQC.ScrollBar.vertical: UI.ScrollBar {}

        ColumnLayout {
            id: form
            width: parent.width
            spacing: 8

            UI.Label {
                text: qsTr("Name")
                size: 12
                color: UI.Style.inkMuted
            }
            UI.TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("First and last name")
            }

            UI.Label {
                Layout.topMargin: 6
                text: qsTr("Email")
                size: 12
                color: UI.Style.inkMuted
            }
            Repeater {
                model: editor.emails
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 6
                    UI.ComboBox {
                        implicitWidth: 110
                        model: editor.emailTypes
                        currentIndex: editor.typeIndex(editor.emailTypes, modelData.type)
                        onActivated: function (i) {
                            editor.emails[index].type = editor.emailTypes[i].value;
                        }
                    }
                    UI.TextField {
                        Layout.fillWidth: true
                        text: modelData.value
                        placeholderText: qsTr("name@example.com")
                        inputMethodHints: Qt.ImhEmailCharactersOnly
                        onTextChanged: {
                            editor.emails[index].value = text;
                            editor.edits++;
                        }
                    }
                    UI.IconButton {
                        iconName: "close"
                        tip: qsTr("Remove")
                        visible: editor.emails.length > 1
                        onClicked: editor.emails = editor.emails.filter((_, i) => i !== index)
                    }
                }
            }
            UI.LinkButton {
                text: qsTr("Add another email")
                onClicked: editor.emails = editor.emails.concat([{ type: "OTHER", value: "" }])
            }

            UI.Label {
                Layout.topMargin: 6
                text: qsTr("Phone")
                size: 12
                color: UI.Style.inkMuted
            }
            Repeater {
                model: editor.phones
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 6
                    UI.ComboBox {
                        implicitWidth: 110
                        model: editor.phoneTypes
                        currentIndex: editor.typeIndex(editor.phoneTypes, modelData.type)
                        onActivated: function (i) {
                            editor.phones[index].type = editor.phoneTypes[i].value;
                        }
                    }
                    UI.TextField {
                        Layout.fillWidth: true
                        text: modelData.value
                        inputMethodHints: Qt.ImhDialableCharactersOnly
                        onTextChanged: {
                            editor.phones[index].value = text;
                            editor.edits++;
                        }
                    }
                    UI.IconButton {
                        iconName: "close"
                        tip: qsTr("Remove")
                        visible: editor.phones.length > 1
                        onClicked: editor.phones = editor.phones.filter((_, i) => i !== index)
                    }
                }
            }
            UI.LinkButton {
                text: qsTr("Add another number")
                onClicked: editor.phones = editor.phones.concat([{ type: "", value: "" }])
            }

            UI.Label {
                Layout.topMargin: 6
                text: qsTr("Company")
                size: 12
                color: UI.Style.inkMuted
            }
            UI.TextField {
                id: companyField
                Layout.fillWidth: true
            }

            UI.Label {
                Layout.topMargin: 6
                text: qsTr("Birthday")
                size: 12
                color: UI.Style.inkMuted
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                UI.ComboBox {
                    id: dayBox
                    implicitWidth: 84
                    model: editor.days
                }
                UI.ComboBox {
                    id: monthBox
                    Layout.fillWidth: true
                    model: editor.months
                }
                UI.TextField {
                    id: yearField
                    implicitWidth: 90
                    placeholderText: qsTr("Year")
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 4
                }
            }

            UI.Label {
                Layout.topMargin: 6
                text: qsTr("Notes")
                size: 12
                color: UI.Style.inkMuted
            }
            QQC.TextArea {
                id: notesField
                Layout.fillWidth: true
                Layout.minimumHeight: 80
                wrapMode: TextEdit.Wrap
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
                font.pixelSize: UI.Style.px(13)
                placeholderTextColor: UI.Style.inkPlaceholder
                padding: 10
                background: Rectangle {
                    radius: 6
                    color: UI.Style.surfaceSunken
                    border.width: 1
                    border.color: notesField.activeFocus ? UI.Style.accent : UI.Style.borderField
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

            UI.Label {
                visible: !editor.key && editor.books.length > 1
                Layout.topMargin: 6
                text: qsTr("Save in")
                size: 12
                color: UI.Style.inkMuted
            }
            UI.ComboBox {
                id: bookBox
                visible: !editor.key && editor.books.length > 1
                Layout.fillWidth: true
                model: editor.books.map(b => ({ value: b.uid, text: b.name }))
            }
        }
    }

    buttons: [
        UI.Button {
            text: qsTr("Cancel")
            onClicked: editor.close()
        },
        UI.Button {
            kind: "primary"
            text: qsTr("Save")
            enabled: editor.edits >= 0 && (nameField.text.trim().length > 0 || editor.emails.some(e => e.value.trim().length > 0) || editor.phones.some(p => p.value.trim().length > 0))
            onClicked: editor.save()
        }
    ]
}
