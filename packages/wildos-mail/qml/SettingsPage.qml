import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Mail's settings (MailMsSetAccounts.dc, MailMsSetMail.dc,
// MailMsSetPrivacy.dc), in the window in place of the mail. Only what Mail
// does today is here; nothing is shown that doesn't work yet.
Rectangle {
    id: page

    property string section: "accounts"
    property string readingPane: "right"

    signal closeRequested()
    signal addAccount()
    signal signIn(string accountId)

    radius: 10
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle
    clip: true

    readonly property var sections: [
        { id: "accounts", name: qsTr("Accounts"), icon: "people" },
        { id: "mail", name: qsTr("Mail"), icon: "layout" },
        { id: "writing", name: qsTr("Writing and signatures"), icon: "signature" },
        { id: "privacy", name: qsTr("Privacy and security"), icon: "shield" },
        { id: "notifications", name: qsTr("Notifications"), icon: "bell" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.preferredWidth: 236
            Layout.fillHeight: true
            Layout.margins: 10
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                UI.IconButton {
                    iconName: "back"
                    tip: qsTr("Back to mail")
                    onClicked: page.closeRequested()
                }
                UI.Label {
                    text: qsTr("Settings")
                    size: 16
                    font.weight: UI.Style.weightSemiBold
                }
            }

            Repeater {
                model: page.sections
                delegate: Rectangle {
                    id: nav
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: 6
                    color: page.section === modelData.id ? UI.Style.surfaceSelected : navMouse.containsMouse ? UI.Style.surfaceHover : "transparent"

                    MouseArea {
                        id: navMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.section = nav.modelData.id
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 10
                        MailIcon {
                            name: nav.modelData.icon
                            size: 16
                            color: page.section === nav.modelData.id ? UI.Style.iconInk : UI.Style.iconMuted
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: nav.modelData.name
                            size: 13
                            font.weight: page.section === nav.modelData.id ? UI.Style.weightSemiBold : UI.Style.weightRegular
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 1
            color: UI.Style.borderSubtle
        }

        QQC.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.min(720, parent.width - 56)
                x: 28
                spacing: 14

                Item {
                    implicitHeight: 14
                }
                UI.Label {
                    text: page.sections.find(s => s.id === page.section).name
                    size: 22
                    font.weight: UI.Style.weightSemiBold
                }
                UI.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    size: 13
                    color: UI.Style.inkMuted
                    text: page.section === "accounts" ? qsTr("Mail accounts on this PC")
                        : page.section === "mail" ? qsTr("Layout, reading and how messages are sorted")
                        : page.section === "writing" ? qsTr("What goes at the end of what you write")
                        : page.section === "privacy" ? qsTr("What senders can learn about you, and what leaves this PC")
                        : qsTr("When Mail lets you know")
                }

                // Accounts
                Repeater {
                    model: page.section === "accounts" ? MailEngine.accounts : []
                    delegate: UI.Card {
                        id: accountCard
                        required property var modelData
                        Layout.fillWidth: true
                        padding: 16

                        ColumnLayout {
                            width: parent.width
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                SenderBadge {
                                    size: 36
                                    initials: MailEngine.initialsOf(accountCard.modelData.fullName || accountCard.modelData.email)
                                    colourIndex: MailEngine.colourOf(accountCard.modelData.email)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    UI.Label {
                                        text: accountCard.modelData.email
                                        size: 14
                                        font.weight: UI.Style.weightSemiBold
                                    }
                                    UI.Label {
                                        Layout.fillWidth: true
                                        text: accountCard.modelData.problem ? accountCard.modelData.problem
                                            : accountCard.modelData.connected ? qsTr("Up to date") : MailEngine.online ? qsTr("Connecting") : qsTr("Offline")
                                        color: accountCard.modelData.problem ? UI.Style.warning : UI.Style.inkMuted
                                        size: 12
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            UI.SettingRow {
                                Layout.fillWidth: true
                                title: qsTr("Name people see")
                                UI.TextField {
                                    width: 240
                                    text: accountCard.modelData.fullName
                                    function save() {
                                        const a = MailEngine.account(accountCard.modelData.id);
                                        a.fullName = text.trim();
                                        MailEngine.updateAccount(a);
                                    }
                                    // Saving rebuilds this card, and the open text menu
                                    // with it; the menu holds focus while it is up.
                                    onEditingFinished: if (!textMenuOpen) save()
                                    onTextMenuFocusLeft: save()
                                }
                            }
                            UI.SettingRow {
                                Layout.fillWidth: true
                                title: qsTr("Account name in Mail")
                                UI.TextField {
                                    width: 240
                                    text: accountCard.modelData.name
                                    function save() {
                                        const a = MailEngine.account(accountCard.modelData.id);
                                        a.name = text.trim() || a.email;
                                        MailEngine.updateAccount(a);
                                    }
                                    // Saving rebuilds this card, and the open text menu
                                    // with it; the menu holds focus while it is up.
                                    onEditingFinished: if (!textMenuOpen) save()
                                    onTextMenuFocusLeft: save()
                                }
                            }
                            UI.SettingRow {
                                Layout.fillWidth: true
                                title: qsTr("Keep all mail on this PC")
                                description: qsTr("Read and search everything offline. Off keeps only what you open.")
                                UI.Toggle {
                                    checked: accountCard.modelData.keepAll
                                    onToggled: {
                                        const a = MailEngine.account(accountCard.modelData.id);
                                        a.keepAll = checked;
                                        MailEngine.updateAccount(a);
                                    }
                                }
                            }
                            RowLayout {
                                spacing: 8
                                UI.Button {
                                    text: qsTr("Sign in again")
                                    size: "small"
                                    iconName: "refresh"
                                    onClicked: page.signIn(accountCard.modelData.id)
                                }
                                UI.Button {
                                    text: qsTr("Remove account")
                                    size: "small"
                                    danger: true
                                    onClicked: removeConfirm.ask(accountCard.modelData)
                                }
                            }
                        }
                    }
                }
                UI.Button {
                    visible: page.section === "accounts"
                    text: qsTr("Add account")
                    iconName: "plus"
                    onClicked: page.addAccount()
                }

                // Mail
                UI.Card {
                    visible: page.section === "mail"
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Reading pane")
                            description: qsTr("Off opens messages in the whole window")
                            UI.ComboBox {
                                model: [{ value: "right", text: qsTr("On the right") }, { value: "off", text: qsTr("Off") }]
                                currentIndex: page.readingPane === "off" ? 1 : 0
                                onActivated: function (i) {
                                    page.readingPane = i === 1 ? "off" : "right";
                                }
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Group messages into conversations")
                            description: qsTr("One row per topic, showing the newest message")
                            UI.Toggle {
                                checked: MailEngine.messages.conversations
                                onToggled: MailEngine.messages.conversations = checked
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Mark as read")
                            description: qsTr("After a message has been open for 2 seconds")
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("After deleting or archiving")
                            description: qsTr("The next message opens")
                        }
                    }
                }

                // Writing and signatures
                Repeater {
                    model: page.section === "writing" ? MailEngine.accounts : []
                    delegate: UI.Card {
                        id: sigCard
                        required property var modelData
                        Layout.fillWidth: true
                        padding: 16
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            UI.Label {
                                text: qsTr("Signature for %1").arg(sigCard.modelData.email)
                                size: 13
                                font.weight: UI.Style.weightSemiBold
                            }
                            QQC.TextArea {
                                id: signatureEdit
                                Layout.fillWidth: true
                                Layout.preferredHeight: 110
                                text: sigCard.modelData.signature
                                wrapMode: TextEdit.Wrap
                                color: UI.Style.ink
                                font.family: UI.Style.fontFamily
                                font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
                                font.pixelSize: UI.Style.px(13)
                                placeholderText: qsTr("For example, your name and phone number")
                                placeholderTextColor: UI.Style.inkPlaceholder
                                background: Rectangle {
                                    radius: 6
                                    color: UI.Style.surfaceSunken
                                    border.width: 1
                                    border.color: UI.Style.borderField
                                }
                                function save() {
                                    const a = MailEngine.account(sigCard.modelData.id);
                                    a.signature = text;
                                    MailEngine.updateAccount(a);
                                }
                                // Saving rebuilds this card, and the open text menu with
                                // it; the menu holds focus while it is up.
                                onEditingFinished: if (!signatureMenu.opened) save()
                                Keys.onPressed: event => {
                                    if (signatureMenu.isMenuKey(event)) {
                                        signatureMenu.openAtCaret();
                                        event.accepted = true;
                                    }
                                }

                                UI.TextMenu {
                                    id: signatureMenu
                                    onFocusLeft: signatureEdit.save()
                                }
                            }
                        }
                    }
                }

                // Privacy and security
                RowLayout {
                    visible: page.section === "privacy"
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: [
                            { n: MailEngine.trackersThisWeek, t: qsTr("trackers blocked this week") },
                            { n: 0, t: qsTr("read receipts sent") },
                            { n: MailEngine.photosCleaned, t: qsTr("photos sent without location") }
                        ]
                        delegate: UI.Card {
                            required property var modelData
                            Layout.fillWidth: true
                            padding: 16
                            ColumnLayout {
                                width: parent.width
                                spacing: 2
                                UI.Label {
                                    text: modelData.n
                                    size: 26
                                    font.weight: UI.Style.weightSemiBold
                                }
                                UI.Label {
                                    Layout.fillWidth: true
                                    text: modelData.t
                                    size: 12
                                    color: UI.Style.inkMuted
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
                UI.Card {
                    visible: page.section === "privacy"
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Pictures from the web")
                            description: qsTr("Loading pictures tells the sender you opened the message, and roughly where you are. Mail asks each time.")
                            UI.Label {
                                text: qsTr("Always ask")
                                size: 13
                                color: UI.Style.inkMuted
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Trackers")
                            description: qsTr("Tiny or hidden pictures that report when you read a message stay blocked, even after Show pictures")
                            UI.Label {
                                text: qsTr("Always blocked")
                                size: 13
                                color: UI.Style.inkMuted
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Read receipts")
                            description: qsTr("When someone asks to know you read their message")
                            UI.Label {
                                text: qsTr("Never sent")
                                size: 13
                                color: UI.Style.inkMuted
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Remove location from photos")
                            description: qsTr("Location, camera and date details are taken out of photos before they're attached")
                            UI.Label {
                                text: qsTr("Always")
                                size: 13
                                color: UI.Style.inkMuted
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("Scripts in messages")
                            description: qsTr("Mail shows messages without running anything in them")
                            UI.Label {
                                text: qsTr("Never run")
                                size: 13
                                color: UI.Style.inkMuted
                            }
                        }
                    }
                }

                // Notifications
                UI.Card {
                    visible: page.section === "notifications"
                    Layout.fillWidth: true
                    UI.SettingRow {
                        width: parent.width
                        title: qsTr("New mail")
                        description: qsTr("A notification for new mail in your Inbox while Mail is open, with Reply, Mark as read and Archive")
                        UI.Toggle {
                            checked: Notifier.enabled
                            onToggled: Notifier.enabled = checked
                        }
                    }
                }

                Item {
                    implicitHeight: 24
                }
            }
        }
    }


    UI.Dialog {
        id: removeConfirm
        property var target: ({})
        function ask(account) {
            target = account;
            open();
        }
        title: qsTr("Remove %1?").arg(target.email || "")
        text: qsTr("Mail and settings kept on this PC for this account are deleted. Nothing is deleted from your email provider.")
        iconName: "warning"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: removeConfirm.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Remove")
                onClicked: {
                    MailEngine.removeAccount(removeConfirm.target.id);
                    removeConfirm.close();
                }
            }
        ]
    }
}
