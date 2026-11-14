import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Writing a message, in the reading pane's place (MailMsCompose.dc): Send
// and Discard, From, To, Cc and Bcc, Subject, the words, the message being
// answered under them, and the photo-location promise at the bottom.
Rectangle {
    id: pane

    readonly property alias composer: composer
    readonly property alias formatter: formatter
    property bool showCc: false
    property bool showBcc: false

    signal closed()
    signal problem(string text)
    signal sentMessage()

    radius: 10
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle
    clip: true

    // True while start() fills the fields, so filling them isn't taken for
    // writing and no empty draft is saved.
    property bool filling: false

    function start(data) {
        filling = true;
        composer.start(data || {});
        to.setText(composer.to);
        cc.setText(composer.cc);
        bcc.setText("");
        showCc = composer.cc.length > 0;
        showBcc = false;
        subject.text = composer.subject;
        editor.text = "";
        if (data && data.body) {
            editor.text = data.body;
        } else if (composer.bodyText.length > 0) {
            editor.text = composer.bodyText.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/\n/g, "<br>");
        }
        filling = false;
        if (to.people.length === 0) {
            to.forceActiveFocus();
        } else {
            editor.forceActiveFocus();
            editor.cursorPosition = 0;
        }
    }
    function sync() {
        composer.to = to.text;
        composer.cc = cc.text;
        composer.bcc = bcc.text;
        composer.subject = subject.text;
        composer.bodyHtml = formatter.html();
        composer.bodyText = formatter.plainText();
    }
    function send() {
        sync();
        if (composer.send()) {
            pane.sentMessage();
            pane.closed();
        }
    }
    function attach() {
        fileDialog.open();
    }

    Composer {
        id: composer
        onProblem: function (text) {
            pane.problem(text);
        }
    }

    Formatter {
        id: formatter
        document: editor.textDocument
        selectionStart: editor.selectionStart
        selectionEnd: editor.selectionEnd
        cursorPosition: editor.cursorPosition
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Attach files")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (const f of selectedFiles) {
                composer.addAttachment(f);
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.topMargin: 12
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                spacing: 8

                UI.Button {
                    kind: "primary"
                    size: "small"
                    text: qsTr("Send")
                    iconName: "send"
                    busy: composer.sending
                    onClicked: pane.send()
                }
                UI.Button {
                    size: "small"
                    text: qsTr("Discard")
                    iconName: "delete"
                    onClicked: {
                        composer.discard();
                        pane.closed();
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                UI.Label {
                    visible: composer.savedAt.length > 0
                    text: qsTr("Draft saved at %1").arg(composer.savedAt)
                    size: 12
                    color: UI.Style.inkMuted
                }
            }

            // From, with a choice when there is more than one account.
            Item {
                Layout.fillWidth: true
                implicitHeight: 40

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: UI.Style.borderSubtle
                }
                UI.Label {
                    x: 4
                    width: 56
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("From")
                    size: 13
                    color: UI.Style.inkMuted
                }
                MouseArea {
                    id: fromArea
                    x: 70
                    width: fromRow.implicitWidth + 8
                    height: parent.height
                    enabled: MailEngine.accounts.length > 1
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: fromMenu.popup(fromArea, 0, fromArea.height)

                    Row {
                        id: fromRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        UI.Label {
                            readonly property var account: MailEngine.account(composer.accountId)
                            text: account.fullName ? qsTr("%1 (%2)").arg(account.fullName).arg(account.name) : (account.email || "")
                            size: 13
                        }
                        MailIcon {
                            visible: fromArea.enabled
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron-down"
                            size: 13
                            color: UI.Style.inkMuted
                        }
                    }
                }
                UI.Menu {
                    id: fromMenu
                    entries: MailEngine.accounts.map(a => ({ text: a.fullName ? a.fullName + " <" + a.email + ">" : a.email, radio: true, checked: a.id === composer.accountId, run: () => { composer.accountId = a.id; composer.touch(); } }))
                }
            }

            RecipientField {
                id: to
                Layout.fillWidth: true
                label: qsTr("To")
                next: pane.showCc ? cc : pane.showBcc ? bcc : subject
                onEdited: composer.touch()

                UI.LinkButton {
                    visible: !pane.showCc
                    text: qsTr("Cc")
                    onClicked: {
                        pane.showCc = true;
                        cc.forceActiveFocus();
                    }
                }
                UI.LinkButton {
                    visible: !pane.showBcc
                    text: qsTr("Bcc")
                    onClicked: {
                        pane.showBcc = true;
                        bcc.forceActiveFocus();
                    }
                }
            }
            RecipientField {
                id: cc
                visible: pane.showCc
                Layout.fillWidth: true
                label: qsTr("Cc")
                next: pane.showBcc ? bcc : subject
                onEdited: composer.touch()
            }
            RecipientField {
                id: bcc
                visible: pane.showBcc
                Layout.fillWidth: true
                label: qsTr("Bcc")
                next: subject
                onEdited: composer.touch()
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: 40

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: UI.Style.borderSubtle
                }
                UI.Label {
                    x: 4
                    width: 56
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Subject")
                    size: 13
                    color: UI.Style.inkMuted
                }
                TextInput {
                    id: subject
                    x: 70
                    width: parent.width - 74
                    anchors.verticalCenter: parent.verticalCenter
                    color: UI.Style.ink
                    selectionColor: UI.Style.textSelection
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(UI.Style.px(14))
                    font.pixelSize: UI.Style.px(14)
                    font.weight: UI.Style.weightMedium
                    clip: true
                    onTextEdited: composer.touch()
                    KeyNavigation.tab: editor
                    Keys.onPressed: event => {
                        if (subjectMenu.isMenuKey(event)) {
                            subjectMenu.openAtCaret();
                            event.accepted = true;
                        }
                    }

                    UI.TextMenu {
                        id: subjectMenu
                    }
                }
            }
        }

        Flow {
            visible: composer.attachments.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.topMargin: 10
            spacing: 8

            Repeater {
                model: composer.attachments
                delegate: Rectangle {
                    id: att
                    required property var modelData
                    width: 220
                    height: 44
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: UI.Style.borderSubtle

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 8
                        MailIcon {
                            name: att.modelData.type.startsWith("image/") ? "picture" : "paperclip"
                            size: 16
                            color: UI.Style.iconMuted
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            UI.Label {
                                Layout.fillWidth: true
                                text: att.modelData.name
                                size: 12.5
                                elide: Text.ElideMiddle
                            }
                            UI.Label {
                                text: att.modelData.photo ? qsTr("%1, location removed").arg(att.modelData.size)
                                    : att.modelData.uncleaned ? qsTr("%1, location couldn't be removed").arg(att.modelData.size) : att.modelData.size
                                size: 11
                                color: att.modelData.uncleaned ? UI.Style.warning : UI.Style.inkMuted
                            }
                        }
                        UI.IconButton {
                            iconName: "close"
                            iconSize: 12
                            tip: qsTr("Remove")
                            implicitWidth: 26
                            implicitHeight: 26
                            onClicked: composer.removeAttachment(att.modelData.index)
                        }
                    }
                }
            }
        }

        QQC.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true
            QQC.ScrollBar.vertical: UI.ScrollBar {}

            ColumnLayout {
                width: pane.width
                spacing: 0

                QQC.TextArea {
                    id: editor
                    Layout.fillWidth: true
                    Layout.maximumWidth: 660
                    Layout.minimumHeight: 100
                    leftPadding: 26
                    rightPadding: 26
                    topPadding: 10
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    color: UI.Style.ink
                    selectionColor: UI.Style.textSelection
                    selectedTextColor: UI.Style.ink
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(UI.Style.px(14.5))
                    font.pixelSize: UI.Style.px(14.5)
                    font.weight: UI.Style.weightRegular
                    placeholderText: qsTr("Write your message")
                    placeholderTextColor: UI.Style.inkPlaceholder
                    background: null
                    onTextChanged: if (!pane.filling) composer.touch()
                    onLinkActivated: function (link) {
                        Qt.openUrlExternally(link);
                    }
                    Keys.onPressed: function (event) {
                        if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                            pane.send();
                            event.accepted = true;
                        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_B) {
                            formatter.toggleBold();
                            event.accepted = true;
                        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_I) {
                            formatter.toggleItalic();
                            event.accepted = true;
                        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_U) {
                            formatter.toggleUnderline();
                            event.accepted = true;
                        } else if (editorMenu.isMenuKey(event)) {
                            editorMenu.openAtCaret();
                            event.accepted = true;
                        }
                    }

                    UI.TextMenu {
                        id: editorMenu
                    }
                }

                // The message being answered, under the reply.
                ColumnLayout {
                    visible: composer.quote.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    Layout.topMargin: 6
                    Layout.bottomMargin: 16
                    spacing: 3

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: quoteColumn.implicitHeight
                        color: "transparent"

                        Rectangle {
                            width: 2
                            height: parent.height
                            color: UI.Style.borderSubtle
                        }
                        ColumnLayout {
                            id: quoteColumn
                            x: 12
                            width: parent.width - 12
                            spacing: 3
                            UI.Label {
                                Layout.fillWidth: true
                                text: composer.quoteHeader
                                size: 12
                                color: UI.Style.inkMuted
                                wrapMode: Text.WordWrap
                            }
                            UI.Label {
                                id: quoteText
                                property bool expanded: false
                                Layout.fillWidth: true
                                text: composer.quote
                                size: 13
                                color: UI.Style.inkMuted
                                wrapMode: Text.WordWrap
                                maximumLineCount: expanded ? 100000 : 2
                                elide: Text.ElideRight
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: quoteText.expanded = !quoteText.expanded
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 14
            implicitHeight: promise.implicitHeight + 20
            radius: 8
            color: UI.Style.successSoft
            border.width: 1
            border.color: Qt.alpha(UI.Style.success, 0.25)

            RowLayout {
                id: promise
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                MailIcon {
                    name: "shield-check"
                    size: 16
                    color: UI.Style.success
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Photos you attach have their location removed before sending.")
                    size: 12.5
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
