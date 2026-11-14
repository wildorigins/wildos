import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// A drive that already has backups, but the keyring doesn't hold their
// password: the one written down when they were set up, on this PC or
// another.
UI.Dialog {
    id: dialog

    property string driveName
    property string error

    signal given(string text)
    signal cancelled()

    function ask() {
        field.text = "";
        open();
        field.forceActiveFocus();
    }

    title: qsTr("Type your backup password")
    text: qsTr("%1 already has backups on it. Type the password you wrote down when they were set up. It's saved in your keyring afterwards.").arg(driveName)

    UI.TextField {
        id: field
        Layout.fillWidth: true
        echoMode: TextInput.Password
        revealable: true
        UI.NoPaste {}
        placeholderText: qsTr("Backup password")
        error: dialog.error.length > 0
        onAccepted: {
            if (text.length > 0) {
                dialog.given(text);
            }
        }
    }
    UI.Label {
        Layout.fillWidth: true
        visible: dialog.error.length > 0
        text: dialog.error
        size: 13
        color: UI.Style.danger
        wrapMode: Text.Wrap
    }

    buttons: [
        UI.Button {
            text: qsTr("Cancel")
            onClicked: {
                dialog.close();
                dialog.cancelled();
            }
        },
        UI.Button {
            kind: "primary"
            text: qsTr("Open backups")
            enabled: field.text.length > 0
            onClicked: dialog.given(field.text)
        }
    ]
}
