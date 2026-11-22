import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Broadcom's own Wi-Fi driver, offered at the top of Network and internet
// when the PC has a Broadcom chip the open drivers don't cover. Without it
// there is no Wi-Fi at all, so getting online another way comes first.
ColumnLayout {
    id: card

    Layout.fillWidth: true
    spacing: 10
    visible: WifiDriver.needed || WifiDriver.busy || WifiDriver.restartNeeded || WifiDriver.error.length > 0

    StatusCard {
        visible: !WifiDriver.restartNeeded
        iconName: "wifi"
        warning: !WifiDriver.busy
        spinning: WifiDriver.busy
        title: WifiDriver.busy ? WifiDriver.step : qsTr("Your Wi-Fi needs a driver")
        description: WifiDriver.busy ? qsTr("This takes a minute or two.")
            : WifiDriver.error.length > 0 ? WifiDriver.error
            : qsTr("%1 needs Broadcom's own driver, which Wild OS can download for you. Get online first with a network cable or your phone's USB internet.").arg(WifiDriver.chipName)

        UI.Button {
            visible: !WifiDriver.busy
            kind: "primary"
            text: qsTr("Get driver")
            onClicked: WifiDriver.install()
        }
    }

    // Secure Boot: the module was built on this PC, so the firmware has to be
    // told to trust this PC's key. It asks once, at the next restart.
    UI.Card {
        Layout.fillWidth: true
        visible: !WifiDriver.busy && WifiDriver.restartNeeded && WifiDriver.enrolCode.length > 0
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
                text: qsTr("Secure Boot is on, so your PC needs to trust the key that signed the Wi-Fi driver. When it restarts, a blue screen appears before Wild OS. Press a key to continue, then choose Enroll MOK, Continue and Yes, type this code and press Enter, then choose Reboot.")
                wrapMode: Text.WordWrap
                color: UI.Style.inkMuted
            }

            UI.Label {
                text: WifiDriver.enrolCode
                size: 28
                font.weight: UI.Style.weightSemiBold
                font.letterSpacing: 3
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Nothing appears on screen as you type it. If the blue screen goes by, Wi-Fi stays off until the key is enrolled. Get the driver again from here for a new code.")
                size: 13
                color: UI.Style.inkMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: !WifiDriver.busy && WifiDriver.restartNeeded

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "restart"
                title: qsTr("Restart to finish")
                description: qsTr("Wi-Fi starts after the restart.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Restart now")
                    onClicked: WifiDriver.restart()
                }
            }
        }
    }
}
