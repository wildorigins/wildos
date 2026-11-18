import QtQuick
import QtQuick.Layouts
import WildOS.TaskManager
import WildOS.UI as UI

// "Run new task", as Windows asks it: type the name of a program and Wild
// OS starts it.
UI.Dialog {
    id: dialog

    function run() {
        const problem = Prefs.runTask(field.text);
        if (problem.length > 0) {
            errorLabel.text = problem;
            return;
        }
        dialog.close();
    }

    title: qsTr("Create new task")
    text: qsTr("Type the name of a program and Wild OS will open it for you.")
    onOpened: {
        field.text = "";
        errorLabel.text = "";
        field.forceActiveFocus();
    }

    UI.TextField {
        id: field
        Layout.fillWidth: true
        placeholderText: qsTr("For example, wildos-files")
        error: errorLabel.text.length > 0
        Accessible.name: qsTr("Open")
        onAccepted: dialog.run()
        onTextChanged: errorLabel.text = ""
    }

    UI.Label {
        id: errorLabel
        visible: text.length > 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        size: 12
        color: UI.Style.danger
    }

    buttons: [
        UI.Button {
            kind: "primary"
            text: qsTr("Open")
            enabled: field.text.trim().length > 0
            onClicked: dialog.run()
        },
        UI.Button {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
    ]
}
