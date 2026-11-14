import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import WildOS.UI as UI
import WildOS.WinMode.Setup

// 9.7, when setup can't go ahead (WinModeProblems.dc): 420 wide, radius
// 16, a 44px tile, a 17px title, plain words on what is wrong and what to
// do, and the one button that helps. Setup never leaves anything half
// installed behind one of these.
Item {
    id: card

    // virtualisation, unsupported, noaccess, space, download or failed
    property string kind
    property real received: 0
    // A download problem because this PC is offline (board 9.7's card), as
    // opposed to one the server or the package system caused.
    property bool offline: false
    property string failure
    // An unexpected error's own words, for reporting it.
    property string detail
    // Over the setup window there is no compositor blur behind a popup, so
    // the card is drawn solid there; on the wallpaper it is glass.
    property bool solid: false

    signal dismissed()

    implicitWidth: 420
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.AlertMessage
    Accessible.name: title.text

    function gb(bytes) {
        return (bytes / 1e9).toLocaleString(Qt.locale(), "f", 1) + " GB";
    }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: card.solid ? UI.Style.windowOpaque : UI.Style.glassDialog
        border.width: 1
        border.color: UI.Style.borderGlass
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: UI.Style.shadowFlyout
            shadowVerticalOffset: 30
            shadowBlur: 1.0
            blurMax: 64
        }
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.bottomMargin: 18
            spacing: 14

            Rectangle {
                implicitWidth: 44
                implicitHeight: 44
                radius: UI.Style.radiusMd
                color: UI.Style.tone(0.07)

                UI.Icon {
                    anchors.centerIn: parent
                    name: card.kind === "space" ? "storage" : card.kind === "download" ? "download" : card.kind === "failed" ? "warning" : "chip"
                    size: 22
                    strokeWidth: 1.9
                    color: card.kind === "download" ? UI.Style.accentText : UI.Style.warning
                }
            }

            UI.Label {
                id: title
                Layout.fillWidth: true
                size: 17
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                Accessible.role: Accessible.Heading
                text: {
                    switch (card.kind) {
                    case "virtualisation": return "Virtualisation is switched off";
                    case "unsupported": return "This PC can't run Windows mode";
                    case "noaccess": return "Windows mode needs the PC's own screen";
                    case "space": return "Not enough space";
                    case "download": return card.offline ? "Download paused" : "Download stopped";
                    default: return "Setup couldn't finish";
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                size: 13
                color: UI.Style.inkAlpha(0.82, 0.76)
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                lineHeight: 1.55
                text: {
                    switch (card.kind) {
                    case "virtualisation":
                        return "Windows mode needs a feature called virtualisation, which is switched off in this PC's firmware settings. It takes about a minute to turn on.";
                    case "unsupported":
                        return "Its processor doesn't have the virtualisation feature Windows mode needs. Many Windows programs still run on the desktop without it, and Office works in the browser.";
                    case "noaccess":
                        return "Virtualisation is on, but it can only be used by whoever is signed in at the PC itself, not from a remote session. Set up Windows mode from the PC.";
                    case "space":
                        return "Windows mode needs about " + Setup.needLabel + " and this PC has " + Setup.freeLabel + " free. Clearing the Downloads folder and emptying the Recycle Bin usually frees enough.";
                    case "download":
                        // Offline: setup waits and carries on by itself once
                        // NetworkManager says the PC is back online. Anything
                        // else (updates holding the package lock, Microsoft
                        // refusing the download) says what actually happened
                        // and waits for Try again.
                        if (card.offline) {
                            return card.received > 0
                                ? "The connection dropped at " + card.gb(card.received) + ". Nothing is lost; setup carries on from where it stopped as soon as you're back online."
                                : "This PC is offline. Nothing is lost; setup carries on from where it stopped as soon as you're back online.";
                        }
                        return (card.failure.length > 0 ? card.failure : "The download stopped.") + " Nothing is lost; Try again carries on from where it stopped.";
                    default:
                        return (card.failure.length > 0 ? card.failure + " " : "") + "Nothing was left half installed.";
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: card.kind === "failed" && card.detail.length > 0
                text: "Details: " + card.detail
                size: 12
                color: UI.Style.inkAlpha(0.66, 0.6)
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            UI.Label {
                Layout.fillWidth: true
                visible: card.kind === "download" && Setup.internet === "metered"
                text: "You're on a metered connection, so you can wait for Wi-Fi."
                size: 13
                color: UI.Style.inkAlpha(0.82, 0.76)
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            Column {
                Layout.fillWidth: true
                visible: card.kind === "virtualisation"
                spacing: 2

                Repeater {
                    model: Setup.canRestartToFirmware
                        ? ["Choose Restart to firmware settings", "Find Intel VT-x, AMD-V or SVM, usually under CPU or Advanced", "Set it to Enabled, then save and exit"]
                        : ["Restart the PC and press its setup key as it starts, often F2, F10 or Delete", "Find Intel VT-x, AMD-V or SVM, usually under CPU or Advanced", "Set it to Enabled, then save and exit"]

                    UI.Label {
                        required property int index
                        required property string modelData
                        width: parent.width
                        text: (index + 1) + ".  " + modelData
                        size: 13
                        color: UI.Style.inkAlpha(0.82, 0.76)
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        lineHeight: 1.6
                    }
                }
            }

            UI.Card {
                Layout.fillWidth: true
                visible: card.kind === "space"

                Column {
                    width: parent.width

                    UI.ListRow {
                        minHeight: 48
                        title: "Downloads"
                        description: Setup.downloadsLabel
                    }

                    UI.ListRow {
                        minHeight: 48
                        title: "Recycle Bin"
                        description: Setup.binLabel
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            bottomLeftRadius: 16
            bottomRightRadius: 16
            color: UI.Style.shade(0.25, 0.04)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: UI.Style.borderSubtle
            }

            RowLayout {
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                UI.Button {
                    visible: card.kind === "virtualisation" || card.kind === "space" || card.kind === "failed"
                        || (card.kind === "download" && Setup.internet === "metered")
                    text: card.kind === "virtualisation" ? "Not now" : card.kind === "download" ? "Wait for Wi-Fi" : card.kind === "failed" ? "Close" : "Cancel"
                    onClicked: card.dismissed()
                }

                UI.Button {
                    id: main
                    kind: "primary"
                    visible: !(card.kind === "virtualisation" && !Setup.canRestartToFirmware)
                    text: {
                        switch (card.kind) {
                        case "virtualisation": return "Restart to firmware settings";
                        case "space": return "Open storage";
                        case "download": return "Try again";
                        case "failed": return "Try again";
                        default: return "Close";
                        }
                    }
                    onClicked: {
                        switch (card.kind) {
                        case "virtualisation": Setup.restartToFirmware(); break;
                        case "space": Setup.openStorage(); break;
                        case "download": Setup.resume(); break;
                        case "failed": card.retry(); break;
                        }
                        card.dismissed();
                    }
                }

                UI.Button {
                    visible: card.kind === "virtualisation" && !Setup.canRestartToFirmware
                    kind: "primary"
                    text: "Close"
                    onClicked: card.dismissed()
                }
            }
        }
    }

    signal retry()

    function takeFocus() {
        main.forceActiveFocus();
    }
}
