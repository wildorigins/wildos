import QtQuick
import QtQuick.Layouts
import WildOS.Remote
import WildOS.UI as UI

// Signing in to a PC (RemoteSignIn.dc): the connection is up and checked,
// and Windows is waiting for an account.
Panel {
    id: dialog

    property var desktop: null
    property string error
    property bool answered: false

    width: 460
    padding: 28

    function ask(d, user, reason) {
        desktop = d;
        error = reason === "wrong" ? qsTr("That didn't work. Check the username and password, then try again.") : "";
        userField.text = user || "";
        password.text = "";
        save.checked = false;
        answered = false;
        open();
        if (userField.text.length > 0) {
            password.forceActiveFocus();
        } else {
            userField.forceActiveFocus();
        }
    }

    onClosed: {
        if (!answered && desktop) {
            desktop.cancelSignIn();
        }
        password.text = "";
    }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            spacing: 14
            Image {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                source: "qrc:/qt/qml/WildOS/Remote/data/org.wildos.remote.svg"
                sourceSize: Qt.size(80, 80)
            }
            ColumnLayout {
                spacing: 2
                UI.Label {
                    text: dialog.desktop ? qsTr("Sign in to %1").arg(dialog.desktop.name) : ""
                    size: 20
                    font.weight: UI.Style.weightBold
                }
                UI.Label {
                    text: dialog.desktop ? dialog.desktop.address : ""
                    size: 13
                    color: UI.Style.inkMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            radius: 10
            color: dialog.error.length > 0 ? Qt.alpha(UI.Style.danger, 0.10) : UI.Style.tone(0.05)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12
                UI.Spinner {
                    visible: dialog.error.length === 0
                    size: 22
                    running: dialog.visible
                }
                UI.Icon {
                    visible: dialog.error.length > 0
                    name: "warning"
                    size: 18
                    color: UI.Style.danger
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    UI.Label {
                        Layout.fillWidth: true
                        text: dialog.error.length > 0 ? qsTr("Windows didn't accept that") : dialog.desktop ? qsTr("Connected to %1").arg(dialog.desktop.name) : ""
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: dialog.error.length > 0 ? dialog.error : qsTr("Its identity checks out. Waiting for you to sign in")
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        UI.Label {
            text: qsTr("Username")
            size: 13
            font.weight: UI.Style.weightSemiBold
        }
        UI.TextField {
            id: userField
            Layout.fillWidth: true
            size: "large"
            leftPadding: 40
            //: %1 is a backslash, written this way because lupdate reads \\n as a line break
            placeholderText: qsTr("Name, or DOMAIN%1name").arg("\\\\")
            UI.Icon {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                name: "account"
                size: 15
                color: UI.Style.iconMuted
            }
        }
        UI.Label {
            text: qsTr("Password")
            size: 13
            font.weight: UI.Style.weightSemiBold
        }
        UI.TextField {
            id: password
            Layout.fillWidth: true
            size: "large"
            leftPadding: 40
            revealable: true
            placeholderText: qsTr("Password")
            onAccepted: go.clicked()
            UI.Icon {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                name: "lock"
                size: 15
                color: UI.Style.iconMuted
            }
        }
        RowLayout {
            spacing: 12
            UI.Toggle {
                id: save
                Accessible.name: saveLabel.text
            }
            UI.Label {
                id: saveLabel
                text: qsTr("Save in my keyring")
                size: 14
            }
        }
        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Your keyring is locked with your Wild OS password, so saved passwords are only readable while you're signed in. Nothing is saved until Windows accepts it.")
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            lineHeight: 1.35
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            Item { Layout.fillWidth: true }
            UI.Button {
                size: "large"
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            UI.Button {
                id: go
                size: "large"
                kind: "primary"
                text: qsTr("Sign in")
                enabled: userField.text.trim().length > 0 && password.text.length > 0
                onClicked: {
                    dialog.answered = true;
                    dialog.desktop.signIn(userField.text.trim(), password.text, save.checked);
                    dialog.close();
                }
            }
        }
    }
}
