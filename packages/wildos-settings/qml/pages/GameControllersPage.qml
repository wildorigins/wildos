import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Game controllers, after the Windows game controller test: each
// controller with its battery, and its buttons and sticks moving live, so
// a person can see that every one works before blaming the game.
PageScroll {
    id: page

    readonly property var controllers: GameControllers.controllers

    Component.onCompleted: GameControllers.active = true
    Component.onDestruction: GameControllers.active = false

    function batteryText(level) {
        switch (level) {
        case "full": return qsTr("Battery full");
        case "medium": return qsTr("Battery half full");
        case "low": return qsTr("Battery low");
        case "empty": return qsTr("Battery almost empty");
        case "wired": return qsTr("Connected by cable");
        default: return qsTr("Connected");
        }
    }

    StatusCard {
        visible: page.controllers.length === 0
        iconName: "games"
        warning: true
        title: qsTr("No game controller connected")
        description: qsTr("Plug in a controller, or pair one in Bluetooth, and it appears here.")

        UI.Button {
            visible: Pages.exists("bluetooth")
            text: "Bluetooth"
            onClicked: Navigator.open("bluetooth")
        }
    }

    // The state changes many times a second while a stick moves, so the
    // repeaters count rather than take the lists, and the rows stay put
    // while their values follow.
    Repeater {
        model: page.controllers.length

        ColumnLayout {
            id: controller
            required property int index
            readonly property var modelData: page.controllers[index] || ({ name: "", battery: "", buttons: [], axes: [] })
            Layout.fillWidth: true
            spacing: 10

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        iconName: "games"
                        title: controller.modelData.name
                        description: page.batteryText(controller.modelData.battery)
                        descriptionColor: controller.modelData.battery === "low" || controller.modelData.battery === "empty" ? UI.Style.warning : UI.Style.inkMuted
                    }

                    UI.SettingRow {
                        visible: controller.modelData.buttons.length > 0
                        title: qsTr("Buttons")
                        description: qsTr("Press a button and it lights up")

                        below: Repeater {
                            model: controller.modelData.buttons.length

                            UI.Chip {
                                required property int index
                                readonly property var button: controller.modelData.buttons[index] || ({ name: "", pressed: false })
                                text: button.name
                                granted: button.pressed
                            }
                        }
                    }

                    Repeater {
                        model: controller.modelData.axes.length

                        UI.SettingRow {
                            id: axisRow
                            required property int index
                            readonly property var modelData: controller.modelData.axes[index] || ({ name: "", value: 0 })
                            title: modelData.name

                            UI.ProgressBar {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 240
                                barHeight: 6
                                from: 0
                                to: 1
                                value: axisRow.modelData.value
                                Accessible.name: axisRow.modelData.name
                            }
                        }
                    }
                }
            }
        }
    }
}
