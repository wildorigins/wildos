import QtQuick
import WildOS.UI as UI

// "Invoice Maker isn't responding" (SystemPrompts.dc, top left). Waiting is
// the safe choice and the default, as on Windows; closing the app ends it
// at once, since a frozen app cannot answer a polite request to close.
PromptCard {
    id: card

    property string appName
    property string iconName
    property string iconSource
    property bool canEnd: true

    signal endTask()
    signal wait()

    width: 440
    title: qsTr("%1 isn't responding").arg(appName)
    text: canEnd ? qsTr("You can wait for it to respond, or close it. If you close it, unsaved work may be lost.")
                 : qsTr("It's running on another computer, so Wild OS can only wait for it to respond.")

    picture: AppPicture {
        iconName: card.iconName
        iconSource: card.iconSource
    }

    buttons: [
        UI.Button {
            visible: card.canEnd
            text: qsTr("Close the app")
            onClicked: card.endTask()
        },
        UI.Button {
            id: waitButton
            kind: "primary"
            text: qsTr("Wait")
            onClicked: card.wait()
        }
    ]

    Keys.onEscapePressed: card.wait()
    function takeFocus() {
        waitButton.forceActiveFocus();
    }
}
