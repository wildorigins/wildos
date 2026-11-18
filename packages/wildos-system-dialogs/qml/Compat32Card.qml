import QtQuick
import WildOS.UI as UI

// "32-bit apps are turned off", when a 32-bit program (Steam, an older
// game) is opened while they are off. Shown by the session service when
// 32bit-blocked, standing in for the program, asks for it. mode is "off",
// "locked" (Strict or Maximum protection, which keep them off) or
// "pending" (turned on, waiting for a restart).
PromptCard {
    id: card

    property string appName
    property string iconName
    property string mode: "off"
    property bool busy: false
    property string error

    signal cancelled()
    signal accepted()

    width: 440
    title: mode === "pending" ? qsTr("Restart to use 32-bit apps") : qsTr("32-bit apps are turned off")
    text: {
        const named = card.appName.length > 0;
        let line;
        if (mode === "pending") {
            line = qsTr("32-bit apps are turned on and start working once you restart. Open apps are asked to save first.");
        } else if (mode === "locked") {
            line = named ? qsTr("%1 is a 32-bit app, and Strict and Maximum protection keep 32-bit apps off. To use it, choose Standard protection in Settings > Privacy and security first.").arg(card.appName)
                         : qsTr("This is a 32-bit app, and Strict and Maximum protection keep 32-bit apps off. To use it, choose Standard protection in Settings > Privacy and security first.");
        } else {
            line = named ? qsTr("%1 is a 32-bit app, and Wild OS keeps them off for security. Turning them on lets it run, but adds a part of the system that has had serious security flaws. It takes a restart.").arg(card.appName)
                         : qsTr("This is a 32-bit app, and Wild OS keeps them off for security. Turning them on lets it run, but adds a part of the system that has had serious security flaws. It takes a restart.");
        }
        return card.error.length > 0 ? card.error + " " + line : line;
    }

    picture: AppPicture {
        iconName: card.iconName.length > 0 ? card.iconName : "application-x-executable"
    }

    buttons: [
        UI.Button {
            id: notNowButton
            text: qsTr("Not now")
            onClicked: card.cancelled()
        },
        UI.Button {
            id: mainButton
            kind: "primary"
            busy: card.busy
            enabled: !card.busy
            text: card.mode === "pending" ? qsTr("Restart now") : card.mode === "locked" ? qsTr("Open Settings") : qsTr("Turn on and restart")
            onClicked: card.accepted()
        }
    ]

    Keys.onEscapePressed: card.cancelled()
    // Enter keeps things as they are: turning 32-bit apps on lowers
    // protection, so it takes a deliberate click (and the password).
    function takeFocus() {
        notNowButton.forceActiveFocus();
    }
}
