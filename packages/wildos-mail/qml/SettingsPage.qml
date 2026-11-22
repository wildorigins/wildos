import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

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


    radius: UI.Style.radiusPanel
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle
    clip: true

    // The sidebar (MailMsSetAccounts.dc and the boards after it), with the
    // words "Find a setting" matches besides each name.
    readonly property var sections: [
        { id: "accounts", name: qsTr("Accounts"), icon: "people", words: qsTr("account add remove sign in default order sync calendar contacts") },
        { id: "writing", name: qsTr("Writing and signatures"), icon: "signature", words: qsTr("signature spelling plain rich text") },
        { id: "replies", name: qsTr("Automatic replies"), icon: "airplane", words: qsTr("away holiday out of office vacation") },
        { id: "rules", name: qsTr("Rules"), icon: "sort", words: qsTr("filter sort move folder") },
        { id: "privacy", name: qsTr("Privacy and security"), icon: "shield", words: qsTr("pictures trackers read receipts location") },
        { id: "notifications", name: qsTr("Notifications"), icon: "bell", words: qsTr("sound alert new mail closed") },
        { id: "transfer", name: qsTr("Import and export"), icon: "send", words: qsTr("pst thunderbird evolution mbox backup restore export import contacts calendars vcard ics") },
        { id: "mail", name: qsTr("Appearance"), icon: "layout", words: qsTr("reading pane layout conversations density preview") }
    ]
    property string findText: ""
    readonly property var shownSections: findText.trim().length === 0 ? sections
        : sections.filter(x => (x.name + " " + x.words).toLowerCase().indexOf(findText.trim().toLowerCase()) >= 0)

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

            UI.SearchField {
                id: findField
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                placeholderText: qsTr("Find a setting")
                onTextChanged: page.findText = text
                onAccepted: if (page.shownSections.length > 0) page.section = page.shownSections[0].id
            }

            Repeater {
                model: page.shownSections
                delegate: Rectangle {
                    id: nav
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: UI.Style.radiusXs
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
                    text: page.section === "accounts" ? qsTr("Mail, calendar and contacts accounts on this PC")
                        : page.section === "mail" ? qsTr("Layout, reading and how messages are sorted")
                        : page.section === "writing" ? qsTr("Signatures for each account, and how new messages start")
                        : page.section === "transfer" ? qsTr("Move mail, contacts and calendars in and out of Wild OS")
                        : page.section === "replies" ? qsTr("A reply that sends itself while you are away")
                        : page.section === "rules" ? qsTr("Sort, tag and tidy mail as it arrives")
                        : page.section === "privacy" ? qsTr("What senders can learn about you, and what leaves this PC")
                        : qsTr("When Mail lets you know")
                }

                SettingsAccounts {
                    visible: page.section === "accounts"
                    Layout.fillWidth: true
                    onAddAccount: page.addAccount()
                    onImportMail: page.section = "transfer"
                    onSignIn: function (accountId) {
                        page.signIn(accountId);
                    }
                    onRemoveAccount: function (account) {
                        removeConfirm.ask(account);
                    }
                }

                SettingsTransfer {
                    visible: page.section === "transfer"
                    Layout.fillWidth: true
                }

                SettingsReplies {
                    visible: page.section === "replies"
                    Layout.fillWidth: true
                    onSignIn: function (accountId) {
                        page.signIn(accountId);
                    }
                }

                SettingsRules {
                    visible: page.section === "rules"
                    Layout.fillWidth: true
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

                SettingsSignatures {
                    visible: page.section === "writing"
                    Layout.fillWidth: true
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
                            title: qsTr("Show company logos")
                            description: qsTr("Logos come from each company's own website and are kept on this PC. No other service is asked.")
                            UI.Toggle {
                                checked: SenderImages.logos
                                onToggled: SenderImages.logos = checked
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
                    // A card sizes itself to one child: the rows go in a column.
                    ColumnLayout {
                        width: parent.width
                        spacing: 0
                        UI.SettingRow {
                            Layout.fillWidth: true
                            title: qsTr("New mail")
                            description: qsTr("A notification for new mail in your Inbox, with Reply, Mark as read and Archive")
                            UI.Toggle {
                                checked: Notifier.enabled
                                onToggled: Notifier.enabled = checked
                            }
                        }
                        UI.SettingRow {
                            Layout.fillWidth: true
                            enabled: Notifier.enabled
                            title: qsTr("While Mail is closed")
                            description: qsTr("Mail keeps checking your Inbox after you close it, so new mail still shows up")
                            UI.Toggle {
                                checked: Notifier.background
                                onToggled: Notifier.background = checked
                            }
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
