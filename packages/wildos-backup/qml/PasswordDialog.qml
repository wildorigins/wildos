import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Backup
import WildOS.UI as UI

// The backup password, to write down: once when a drive is set up, and
// again from Settings. Said plainly: the keyring is the only other copy,
// and without either the backups can't be opened.
UI.Dialog {
    id: dialog

    // "new" after setting a drive up; "show" from Settings.
    property string mode: "show"

    signal done()

    title: mode === "new" ? qsTr("Write down your backup password") : qsTr("Your backup password")
    text: qsTr("Your backups are encrypted with this password. It's saved in your keyring, so Backup never asks for it. But if your keyring is lost, for example when you move to a new PC or reinstall, you'll need this password to get your files back. Nobody can open your backups without it, not even Wild OS.")
    closePolicy: mode === "new" ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape
    onClosed: {
        Setup.hidePassword();
        dialog.done();
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.preferredHeight: 56
        radius: 8
        color: BackupStyle.field
        border.width: 1
        border.color: BackupStyle.fieldEdge

        TextEdit {
            id: passwordText
            anchors.centerIn: parent
            text: Setup.revealing ? qsTr("Opening your keyring") : Setup.revealError.length > 0 ? Setup.revealError : Setup.password
            font.family: Setup.password.length > 0 ? "monospace" : UI.Style.fontFamily
            font.pixelSize: Setup.password.length > 0 ? UI.Style.px(19) : UI.Style.px(13)
            font.letterSpacing: Setup.password.length > 0 ? 1 : 0
            color: Setup.password.length > 0 ? UI.Style.ink : UI.Style.inkMuted
            readOnly: true
            selectByMouse: true
            selectedTextColor: UI.Style.ink
            selectionColor: UI.Style.textSelection
            Accessible.name: qsTr("Backup password")
            Keys.onPressed: event => {
                if (passwordMenu.isMenuKey(event)) {
                    passwordMenu.openAtCaret();
                    event.accepted = true;
                }
            }

            UI.TextMenu {
                id: passwordMenu
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: dialog.mode === "new"
        text: qsTr("Keep it somewhere safe away from this PC, like a notebook. You can see it again later in Settings.")
        size: 13
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
    }

    buttons: [
        UI.Button {
            text: qsTr("Copy")
            iconName: "copy"
            enabled: Setup.password.length > 0
            onClicked: {
                passwordText.selectAll();
                passwordText.copy();
                passwordText.deselect();
            }
        },
        UI.Button {
            kind: "primary"
            text: dialog.mode === "new" ? qsTr("I've written it down") : qsTr("Done")
            onClicked: dialog.close()
        }
    ]
}
