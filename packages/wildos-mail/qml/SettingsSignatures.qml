import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Settings, Writing and signatures (MailMsSetSignatures.dc): named
// signatures with an editor, which one each account adds to new messages
// and to replies and forwards, and how messages are written.
ColumnLayout {
    id: page

    property string current: MailEngine.signatures.length > 0 ? MailEngine.signatures[0].id : ""
    readonly property var signature: MailEngine.signatures.find(s => s.id === current) || null

    spacing: 14

    // The open signature is saved as it is typed, a moment after the last
    // change, and when another is chosen.
    function save() {
        if (signature && !loading) {
            MailEngine.saveSignature(current, nameField.text.trim() || qsTr("Signature"), formatter.html());
        }
    }
    property bool loading: false
    function load() {
        loading = true;
        nameField.text = signature ? signature.name : "";
        editor.text = signature ? signature.html : "";
        loading = false;
    }
    onCurrentChanged: load()
    Component.onCompleted: load()
    Timer {
        id: saveTimer
        interval: 600
        onTriggered: page.save()
    }

    UI.Card {
        Layout.fillWidth: true
        padding: 12

        RowLayout {
            width: parent.width
            spacing: 12

            // The signatures.
            ColumnLayout {
                Layout.preferredWidth: 230
                Layout.alignment: Qt.AlignTop
                spacing: 4

                UI.Button {
                    Layout.fillWidth: true
                    text: qsTr("New signature")
                    iconName: "plus"
                    onClicked: {
                        saveTimer.stop();
                        page.save();
                        page.current = MailEngine.addSignature(qsTr("Signature %1").arg(MailEngine.signatures.length + 1));
                        nameField.forceActiveFocus();
                        nameField.selectAll();
                    }
                }
                Repeater {
                    model: MailEngine.signatures
                    delegate: Rectangle {
                        id: sigRow
                        required property var modelData
                        readonly property bool chosen: page.current === modelData.id
                        Layout.fillWidth: true
                        implicitHeight: 46
                        radius: UI.Style.radiusXs
                        // Fades in on hover and clears at once, so moving along a list
                        // never lights two items. It comes before the colour so it sees
                        // the change in hover first.
                        Behavior on color {
                            enabled: sigMouse.containsMouse
                            ColorAnimation { duration: 80 }
                        }
                        color: chosen ? UI.Style.surfaceSelected : sigMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
                        Rectangle {
                            visible: sigRow.chosen
                            width: 3
                            height: 18
                            radius: 1.5
                            anchors.verticalCenter: parent.verticalCenter
                            color: UI.Style.accent
                        }
                        MouseArea {
                            id: sigMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                saveTimer.stop();
                                page.save();
                                page.current = sigRow.modelData.id;
                            }
                        }
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 2
                            UI.Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                text: sigRow.modelData.name
                                size: 13
                                font.weight: sigRow.chosen ? UI.Style.weightSemiBold : UI.Style.weightRegular
                                elide: Text.ElideRight
                            }
                            UI.Label {
                                Layout.fillWidth: true
                                text: sigRow.modelData.html.replace(/<[^>]*>/g, " ").replace(/&nbsp;/g, " ").replace(/\s+/g, " ").trim()
                                size: 11.5
                                color: UI.Style.inkMuted
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                UI.Label {
                    visible: MailEngine.signatures.length === 0
                    Layout.fillWidth: true
                    text: qsTr("No signatures yet.")
                    size: 12
                    color: UI.Style.inkMuted
                }
            }

            // The editor.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8
                enabled: page.signature !== null

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    UI.Label {
                        text: qsTr("Name")
                        size: 13
                    }
                    UI.TextField {
                        id: nameField
                        Layout.preferredWidth: 220
                        onTextEdited: saveTimer.restart()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    UI.IconButton {
                        iconName: "delete"
                        tip: qsTr("Delete signature")
                        onClicked: {
                            const gone = page.current;
                            const rest = MailEngine.signatures.filter(s => s.id !== gone);
                            page.loading = true;
                            page.current = rest.length > 0 ? rest[0].id : "";
                            page.loading = false;
                            MailEngine.removeSignature(gone);
                            page.load();
                        }
                    }
                }
                RowLayout {
                    spacing: 2
                    UI.IconButton {
                        iconName: "bold"
                        tip: qsTr("Bold")
                        checked: formatter.bold
                        onClicked: formatter.toggleBold()
                    }
                    UI.IconButton {
                        iconName: "italic"
                        tip: qsTr("Italic")
                        checked: formatter.italic
                        onClicked: formatter.toggleItalic()
                    }
                    UI.IconButton {
                        iconName: "underline"
                        tip: qsTr("Underline")
                        checked: formatter.underline
                        onClicked: formatter.toggleUnderline()
                    }
                    UI.IconButton {
                        id: colourButton
                        iconName: "palette"
                        tip: qsTr("Text colour")
                        onClicked: colourMenu.popup(colourButton, 0, colourButton.height)
                    }
                    UI.IconButton {
                        iconName: "link"
                        tip: qsTr("Link")
                        onClicked: linkDialog.open()
                    }
                    UI.IconButton {
                        iconName: "image"
                        tip: qsTr("Picture")
                        onClicked: picturePicker.open()
                    }
                }
                QQC.TextArea {
                    id: editor
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    color: UI.Style.ink
                    font.family: UI.Style.fontFamily
                    font.pixelSize: UI.Style.px(13)
                    placeholderText: qsTr("For example, your name and phone number")
                    placeholderTextColor: UI.Style.inkPlaceholder
                    onTextChanged: if (!page.loading) saveTimer.restart()
                    background: Rectangle {
                        radius: UI.Style.radiusXs
                        color: UI.Style.surfaceSunken
                        border.width: 1
                        border.color: editor.activeFocus ? UI.Style.accent : UI.Style.borderField
                    }
                }
            }
        }
    }

    Formatter {
        id: formatter
        document: editor.textDocument
        selectionStart: editor.selectionStart
        selectionEnd: editor.selectionEnd
        cursorPosition: editor.cursorPosition
    }

    // Which signature goes where.
    UI.Card {
        Layout.fillWidth: true
        padding: 16
        visible: MailEngine.accounts.length > 0

        ColumnLayout {
            width: parent.width
            spacing: 10
            UI.Label {
                text: qsTr("Add a signature automatically")
                size: 14
                font.weight: UI.Style.weightSemiBold
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Account")
                    size: 12
                    color: UI.Style.inkMuted
                }
                UI.Label {
                    Layout.preferredWidth: 200
                    text: qsTr("New messages")
                    size: 12
                    color: UI.Style.inkMuted
                }
                UI.Label {
                    Layout.preferredWidth: 200
                    text: qsTr("Replies and forwards")
                    size: 12
                    color: UI.Style.inkMuted
                }
            }
            Repeater {
                model: MailEngine.accounts
                delegate: RowLayout {
                    id: useRow
                    required property var modelData
                    readonly property var choices: [{ value: "", text: qsTr("None") }].concat(MailEngine.signatures.map(s => ({ value: s.id, text: s.name })))
                    Layout.fillWidth: true
                    spacing: 12
                    UI.Label {
                        Layout.fillWidth: true
                        text: useRow.modelData.email
                        size: 13
                        elide: Text.ElideRight
                    }
                    UI.ComboBox {
                        Layout.preferredWidth: 200
                        accessibleName: qsTr("Signature for new messages from %1").arg(useRow.modelData.email)
                        model: useRow.choices
                        currentIndex: Math.max(0, useRow.choices.findIndex(c => c.value === (useRow.modelData.signatureNew || "")))
                        onActivated: function (i) {
                            const a = MailEngine.account(useRow.modelData.id);
                            a.signatureNew = useRow.choices[i].value;
                            MailEngine.updateAccount(a);
                        }
                    }
                    UI.ComboBox {
                        Layout.preferredWidth: 200
                        accessibleName: qsTr("Signature for replies and forwards from %1").arg(useRow.modelData.email)
                        model: useRow.choices
                        currentIndex: Math.max(0, useRow.choices.findIndex(c => c.value === (useRow.modelData.signatureReply || "")))
                        onActivated: function (i) {
                            const a = MailEngine.account(useRow.modelData.id);
                            a.signatureReply = useRow.choices[i].value;
                            MailEngine.updateAccount(a);
                        }
                    }
                }
            }
        }
    }

    // How messages are written.
    UI.Card {
        Layout.fillWidth: true
        padding: 16

        ColumnLayout {
            width: parent.width
            spacing: 10
            UI.Label {
                text: qsTr("Writing")
                size: 14
                font.weight: UI.Style.weightSemiBold
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Check spelling as you type")
                UI.Toggle {
                    checked: prefs.spelling
                    onToggled: prefs.spelling = checked
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Write messages in")
                RowLayout {
                    spacing: 6
                    UI.SegmentButton {
                        text: qsTr("Rich text")
                        checked: !prefs.plain
                        onClicked: prefs.plain = false
                    }
                    UI.SegmentButton {
                        text: qsTr("Plain text")
                        checked: prefs.plain
                        onClicked: prefs.plain = true
                    }
                }
            }
        }
    }

    Settings {
        id: prefs
        category: "writing"
        property bool spelling: true
        property bool plain: false
    }

    UI.Menu {
        id: colourMenu
        entries: [
            { text: qsTr("Black"), run: () => formatter.setColour("#202124") },
            { text: qsTr("Grey"), run: () => formatter.setColour("#5f6368") },
            { text: qsTr("Blue"), run: () => formatter.setColour("#1a5fd0") },
            { text: qsTr("Green"), run: () => formatter.setColour("#1e7d3a") },
            { text: qsTr("Orange"), run: () => formatter.setColour("#b85c00") },
            { text: qsTr("Red"), run: () => formatter.setColour("#c42b1c") },
            { text: qsTr("Purple"), run: () => formatter.setColour("#7a3fb5") }
        ]
    }

    UI.Dialog {
        id: linkDialog
        title: qsTr("Insert link")
        onOpened: {
            linkUrl.text = "";
            linkUrl.forceActiveFocus();
        }
        UI.TextField {
            id: linkUrl
            Layout.fillWidth: true
            placeholderText: "https://"
            onAccepted: linkInsert.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: linkDialog.close()
            },
            UI.Button {
                id: linkInsert
                kind: "primary"
                text: qsTr("Insert")
                enabled: linkUrl.text.trim().length > 0
                onClicked: {
                    const url = /^[a-z]+:/i.test(linkUrl.text.trim()) ? linkUrl.text.trim() : "https://" + linkUrl.text.trim();
                    formatter.insertLink(url, "");
                    linkDialog.close();
                }
            }
        ]
    }

    FileDialog {
        id: picturePicker
        title: qsTr("Picture")
        nameFilters: [qsTr("Pictures (*.png *.jpg *.jpeg *.gif *.webp)")]
        onAccepted: formatter.insertPicture(selectedFile)
    }
}
