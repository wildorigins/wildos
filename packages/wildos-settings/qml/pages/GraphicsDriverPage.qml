import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Display > Graphics driver, shown on PCs with an NVIDIA card: the open
// driver Wild OS ships, or NVIDIA's own from Debian (Graphics,
// settings-graphics-driver). Everything happens here; with Secure Boot on,
// the one step outside Wild OS is typing a code into the firmware's own blue
// screen at the next restart, so the page shows that code and what to press.
PageScroll {
    id: page

    // What the cards show as chosen: what is installed, until a card is picked.
    property string choice: Graphics.nvidiaInstalled ? "nvidia" : "open"
    readonly property string installed: Graphics.nvidiaInstalled ? "nvidia" : "open"

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    Connections {
        target: Graphics
        function onChanged() {
            if (!Graphics.busy && Graphics.error.length > 0) {
                notice.show(qsTr("The driver was not changed"), Graphics.error, true);
                page.choice = page.installed;
            }
        }
    }

    // The first start-up with NVIDIA's driver never showed a desktop, and
    // graphics-driver-trial went back to the open driver.
    UI.Card {
        Layout.fillWidth: true
        visible: Graphics.trialFailed && !Graphics.nvidiaInstalled && !Graphics.busy
        tinted: true
        padding: 18

        ColumnLayout {
            width: parent.width
            spacing: 8

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("NVIDIA's driver did not start on this PC")
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.WordWrap
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("The desktop never appeared with it, so Wild OS put the open driver back. What the PC was doing at the time is saved in /var/log/wildos/graphics-driver.")
                color: UI.Style.inkMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "chip"
                title: Graphics.nvidiaName
                description: Graphics.driverInUse === "nvidia" ? qsTr("Using NVIDIA's driver")
                    : Graphics.driverInUse === "nouveau" ? qsTr("Using the open driver")
                    : qsTr("No driver is using it yet")
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Driver")
        }

        UI.ChoiceCard {
            Layout.fillWidth: true
            title: qsTr("Open driver (built in)")
            description: qsTr("Comes with Wild OS and works on every NVIDIA card. Fine for everyday use, slower in games and 3D.")
            showIndicator: true
            checkable: false
            checked: page.choice === "open"
            enabled: !Graphics.busy
            onClicked: page.choice = "open"
        }

        UI.ChoiceCard {
            Layout.fillWidth: true
            title: qsTr("NVIDIA's driver")
            description: qsTr("Much faster in games and 3D. Made by NVIDIA, closed source, installed from Debian. Supports GeForce GTX 900 series cards up to RTX 40 series; RTX 50 cards keep the open driver for now.")
            showIndicator: true
            checkable: false
            checked: page.choice === "nvidia"
            enabled: !Graphics.busy
            onClicked: page.choice = "nvidia"
        }

        UI.Button {
            visible: page.choice !== page.installed && !Graphics.busy
            text: page.choice === "nvidia" ? qsTr("Install NVIDIA's driver") : qsTr("Go back to the open driver")
            onClicked: Graphics.useDriver(page.choice)
        }
    }

    // While the helper works: its step, under a bar that moves.
    UI.Card {
        Layout.fillWidth: true
        visible: Graphics.busy
        padding: 18

        ColumnLayout {
            width: parent.width
            spacing: 10

            UI.Label {
                Layout.fillWidth: true
                text: Graphics.step + "…"
                wrapMode: Text.WordWrap
            }

            UI.ProgressBar {
                Layout.fillWidth: true
                indeterminate: true
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("This takes a few minutes. Save your work while it runs: once it finishes, apps may not open until the PC restarts.")
                size: 13
                color: UI.Style.inkMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    // Secure Boot: the module was built on this PC, so the firmware has to be
    // told to trust this PC's key. It asks once, at the next restart.
    UI.Card {
        Layout.fillWidth: true
        visible: !Graphics.busy && Graphics.enrolCode.length > 0
        padding: 18

        ColumnLayout {
            width: parent.width
            spacing: 12

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("One more step at the next restart")
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.WordWrap
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Secure Boot is on, so your PC needs to trust the key that signed NVIDIA's driver. When it restarts, a blue screen appears before Wild OS. Press a key to continue, then choose Enroll MOK, Continue and Yes, type this code and press Enter, then choose Reboot.")
                wrapMode: Text.WordWrap
                color: UI.Style.inkMuted
            }

            UI.Label {
                text: Graphics.enrolCode
                size: 28
                font.weight: UI.Style.weightSemiBold
                font.letterSpacing: 3
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Nothing appears on screen as you type it. If the blue screen goes by, Wild OS starts with basic graphics until the key is enrolled. Install again from here to get a new code.")
                size: 13
                color: UI.Style.inkMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: !Graphics.busy && Graphics.restartNeeded

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "restart"
                title: qsTr("Restart to finish")
                description: Graphics.nvidiaInstalled
                    ? qsTr("Restart now: apps may not open until you do. If the desktop does not appear with NVIDIA's driver, leave the PC on. Wild OS puts the open driver back and restarts by itself within a few minutes.")
                    : qsTr("Restart now: apps may not open until you do. The PC starts with the open driver.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Restart now")
                    onClicked: Graphics.restart()
                }
            }
        }
    }
}
