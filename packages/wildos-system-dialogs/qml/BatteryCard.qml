import QtQuick
import WildOS.UI as UI

// "Battery low: 10%" (SystemPrompts.dc, top middle). Battery saver is the
// power-saver profile; its button is only there when the computer has one
// and it is not already on.
PromptCard {
    id: card

    property int percent: 10
    property int minutes: 0
    property bool saver: false
    property bool saverOn: false

    signal dismissed()
    signal saverChosen()

    readonly property bool offerSaver: saver && !saverOn

    function timeLeft(m) {
        if (m <= 0) {
            return "";
        }
        if (m < 60) {
            return (m === 1 ? qsTr("About 1 minute left.") : qsTr("About %1 minutes left.").arg(m)) + " ";
        }
        const h = Math.floor(m / 60);
        const rest = m % 60;
        let line;
        if (rest === 0) {
            line = h === 1 ? qsTr("About 1 hour left.") : qsTr("About %1 hours left.").arg(h);
        } else if (h === 1) {
            line = rest === 1 ? qsTr("About 1 hour 1 minute left.") : qsTr("About 1 hour %1 minutes left.").arg(rest);
        } else {
            line = rest === 1 ? qsTr("About %1 hours 1 minute left.").arg(h) : qsTr("About %1 hours %2 minutes left.").arg(h).arg(rest);
        }
        return line + " ";
    }

    width: 420
    title: qsTr("Battery low: %1%").arg(percent)
    text: timeLeft(minutes) + (offerSaver ? qsTr("Plug in soon, or turn on battery saver to make it last longer.")
                          : saverOn ? qsTr("Plug in soon. Battery saver is already on.")
                          : qsTr("Plug in soon."))

    picture: BatteryPicture {}

    buttons: [
        UI.Button {
            id: dismissButton
            kind: card.offerSaver ? "secondary" : "primary"
            text: qsTr("Dismiss")
            onClicked: card.dismissed()
        },
        UI.Button {
            id: saverButton
            visible: card.offerSaver
            kind: "primary"
            text: qsTr("Turn on battery saver")
            onClicked: card.saverChosen()
        }
    ]

    Keys.onEscapePressed: card.dismissed()
    function takeFocus() {
        (offerSaver ? saverButton : dismissButton).forceActiveFocus();
    }
}
