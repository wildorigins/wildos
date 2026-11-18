import QtQuick
import WildOS.UI as UI

// Something the user asked for did not happen: the reason, in Files' words.
DeskDialog {
    id: dialog

    title: qsTr("That did not work")
    defaultAction: () => dialog.close()

    function show(message) {
        text = message;
        present();
    }

    buttons: [
        UI.Button {
            kind: "primary"
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    ]
}
