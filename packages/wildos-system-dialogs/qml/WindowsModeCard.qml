import QtQuick
import WildOS.UI as UI

// "This app needs Windows mode" (SystemPrompts.dc, bottom left), from the
// App Bridge when a Windows app is opened while Windows app support is off
// in Settings. Use Windows mode turns it back on and opens the app.
PromptCard {
    id: card

    property string appName
    property string iconName
    property string iconSource

    signal cancelled()
    signal accepted()

    width: 440
    title: qsTr("This app needs Windows mode")
    text: qsTr("%1 is a Windows app, and Windows apps are turned off on this computer. It can run in Windows mode instead and will still open as a normal window.").arg(appName)

    picture: AppPicture {
        iconName: card.iconName.length > 0 ? card.iconName : "wildos-windows-app"
        iconSource: card.iconSource
    }

    buttons: [
        UI.Button {
            text: qsTr("Cancel")
            onClicked: card.cancelled()
        },
        UI.Button {
            id: useButton
            kind: "primary"
            text: qsTr("Use Windows mode")
            onClicked: card.accepted()
        }
    ]

    Keys.onEscapePressed: card.cancelled()
    function takeFocus() {
        useButton.forceActiveFocus();
    }
}
