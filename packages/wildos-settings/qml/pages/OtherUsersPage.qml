import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Other users, after Windows 11: Add account, then everyone else on this
// computer with Change account type and Remove. Every account made here can
// sign in (it joins the console group the installer's accounts are in), and
// administrators get the sudo and printer groups. An administrator's
// password is asked for each change, as Windows asks through UAC.
PageScroll {
    id: page

    property var target: null

    Connections {
        target: Users
        function onDone(what, message) {
            if (what === "add") {
                addDialog.close();
                page.target = null;
                notice.show("Account added", message, false);
            } else if (what === "remove" || what === "type") {
                notice.show(what === "remove" ? "Account removed" : "Account type changed", message, false);
            }
        }
        function onFailed(what, reason) {
            if (what === "add") {
                addDialog.error = reason;
            } else if (what === "remove" || what === "type") {
                notice.show(what === "remove" ? "The account was not removed" : "The account type was not changed", reason, true);
            }
        }
    }

    Notice {
        id: notice
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "people"
                iconTile: true
                minHeight: 68
                title: "Add other user"
                description: "Each person gets their own files, settings and password"

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Add account"
                    kind: "primary"
                    busy: Users.busy === "add"
                    enabled: Users.busy.length === 0
                    onClicked: addDialog.open()
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Other users"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: Users.others

                    UI.SettingRow {
                        id: userRow
                        required property var modelData
                        title: modelData.realName
                        description: modelData.userName + ", " + (modelData.admin ? "administrator" : "standard user")
                        minHeight: 68

                        leading: UI.Avatar {
                            size: 40
                            source: userRow.modelData.icon
                        }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Change account type"
                            busy: Users.busy === "type" && page.target !== null && page.target.userName === userRow.modelData.userName
                            enabled: Users.busy.length === 0
                            onClicked: {
                                page.target = userRow.modelData;
                                typeDialog.open();
                            }
                        }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Remove"
                            busy: Users.busy === "remove" && page.target !== null && page.target.userName === userRow.modelData.userName
                            enabled: Users.busy.length === 0
                            onClicked: {
                                page.target = userRow.modelData;
                                removeDialog.open();
                            }
                        }
                    }
                }

                UI.SettingRow {
                    visible: Users.others.length === 0
                    iconName: "account"
                    title: "Nobody else has an account yet"
                    description: "People you add appear here, and on the sign-in screen."
                }
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: !Users.me.admin
        text: "You are a standard user, so Wild OS will ask for an administrator's password before changing other accounts."
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Dialog {
        id: addDialog

        property string error
        property bool nameEdited: false
        readonly property string nameProblem: userField.text.length > 0 ? Users.checkUserName(userField.text) : ""
        readonly property string problem: {
            if (nameProblem.length > 0) {
                return nameProblem;
            }
            if (/[:,]/.test(fullField.text)) {
                return "A name can't contain a colon or comma.";
            }
            if (passwordField.text.length > 0 && passwordField.text.length < 8) {
                return "Use at least 8 characters for the password.";
            }
            if (confirmField.text.length > 0 && confirmField.text !== passwordField.text) {
                return "The two passwords don't match.";
            }
            return "";
        }
        readonly property bool ready: fullField.text.trim().length > 0 && userField.text.length > 0 && problem.length === 0
            && passwordField.text.length >= 8 && confirmField.text === passwordField.text

        title: "Create an account"
        text: "Who will use this computer? They sign in with the user name and password you choose here, and can change the password later in Settings."
        onAboutToShow: {
            error = "";
            nameEdited = false;
            fullField.text = "";
            userField.text = "";
            passwordField.text = "";
            confirmField.text = "";
            typeBox.currentIndex = 0;
            fullField.forceActiveFocus();
        }

        UI.Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            text: "Name"
            size: 12
            color: UI.Style.inkMuted
        }

        UI.TextField {
            id: fullField
            Layout.fillWidth: true
            placeholderText: "For example Alex Morgan"
            onTextChanged: {
                addDialog.error = "";
                if (!addDialog.nameEdited) {
                    userField.text = Users.suggestUserName(text);
                }
            }
            onAccepted: userField.forceActiveFocus()
        }

        UI.Label {
            Layout.fillWidth: true
            text: "User name, for signing in"
            size: 12
            color: UI.Style.inkMuted
        }

        UI.TextField {
            id: userField
            Layout.fillWidth: true
            placeholderText: "For example alex"
            error: addDialog.nameProblem.length > 0
            onTextEdited: addDialog.nameEdited = true
            onAccepted: passwordField.forceActiveFocus()
        }

        UI.Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: "Password"
            size: 12
            color: UI.Style.inkMuted
        }

        UI.TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Password"
            revealable: true
            UI.NoPaste {}
            error: text.length > 0 && text.length < 8
            onAccepted: confirmField.forceActiveFocus()
        }

        StrengthLine {
            Layout.fillWidth: true
            password: passwordField.text
            names: [userField.text, fullField.text]
        }

        UI.TextField {
            id: confirmField
            Layout.fillWidth: true
            placeholderText: "Confirm the password"
            revealable: true
            UI.NoPaste {}
            error: text.length > 0 && text !== passwordField.text
            onAccepted: createButton.clicked()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 12

            UI.Label {
                Layout.fillWidth: true
                text: "Account type"
            }

            UI.ComboBox {
                id: typeBox
                Accessible.name: "Account type"
                model: [{ value: false, text: "Standard user" }, { value: true, text: "Administrator" }]
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: addDialog.error.length > 0 ? addDialog.error : addDialog.problem
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: createButton
                text: "Create account"
                kind: "primary"
                busy: Users.busy === "add"
                enabled: addDialog.ready && Users.busy.length === 0
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    addDialog.error = "";
                    Users.addUser(fullField.text, userField.text, passwordField.text, typeBox.currentValue);
                }
            },
            UI.Button {
                text: "Cancel"
                enabled: Users.busy !== "add"
                onClicked: addDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: typeDialog
        title: "Change account type"
        text: page.target ? "Administrators can install apps, change settings for everyone and manage other accounts." : ""
        onAboutToShow: kindBox.currentIndex = page.target && page.target.admin ? 1 : 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            UI.Avatar {
                size: 40
                source: page.target ? page.target.icon : ""
            }

            UI.Label {
                Layout.fillWidth: true
                text: page.target ? page.target.realName : ""
                font.weight: UI.Style.weightSemiBold
            }

            UI.ComboBox {
                id: kindBox
                Accessible.name: "Account type"
                model: [{ value: false, text: "Standard user" }, { value: true, text: "Administrator" }]
            }
        }

        buttons: [
            UI.Button {
                text: "OK"
                kind: "primary"
                onClicked: {
                    typeDialog.close();
                    if (page.target && kindBox.currentValue !== page.target.admin) {
                        Users.setAdmin(page.target.userName, kindBox.currentValue);
                    }
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: typeDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: removeDialog
        title: page.target ? "Delete " + page.target.realName + "'s account and data?" : ""
        text: "Deleting this account removes all of its files from this computer, including the desktop, documents, downloads, pictures, music and videos. Back up anything they want to keep first."
        buttons: [
            UI.Button {
                text: "Delete account and data"
                kind: "primary"
                onClicked: {
                    removeDialog.close();
                    if (page.target) {
                        Users.removeUser(page.target.userName);
                    }
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: removeDialog.close()
            }
        ]
    }
}
