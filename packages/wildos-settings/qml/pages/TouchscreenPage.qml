import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Touchscreen, after Windows 11's Touch settings and the old Tablet PC
// settings' "which screen is this": turn touch on or off, and tie each
// touchscreen to the screen it sits on, for a PC with more than one.
// KWin applies each change straight away and keeps it for the device.
PageScroll {
    id: page

    readonly property var screens: InputDevices.touchscreens

    ScreenChoices {
        id: screenChoices
    }

    StatusCard {
        visible: page.screens.length === 0
        iconName: "display"
        warning: true
        title: qsTr("No touchscreen connected")
        description: qsTr("A touchscreen built into this PC or plugged in appears here.")
    }

    Repeater {
        model: page.screens

        ColumnLayout {
            id: touchscreen
            required property var modelData
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                visible: page.screens.length > 1
                text: touchscreen.modelData.name
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        visible: touchscreen.modelData.supportsDisableEvents === true
                        iconName: "display"
                        title: qsTr("Touch")
                        description: qsTr("Use your fingers on the screen")
                        status: touchscreen.modelData.enabled === true ? qsTr("On") : qsTr("Off")

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: touchscreen.modelData.enabled === true
                            Accessible.name: qsTr("Touch on %1").arg(touchscreen.modelData.name)
                            onToggled: InputDevices.setDevice(touchscreen.modelData.sysName, "enabled", checked)
                        }
                    }

                    UI.SettingRow {
                        title: qsTr("Screen")
                        description: screenChoices.screenCount > 1 ? qsTr("The screen this touchscreen is part of") : qsTr("Only one screen is connected")
                        enabled: screenChoices.screenCount > 1

                        UI.ComboBox {
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("Screen for %1").arg(touchscreen.modelData.name)
                            model: screenChoices.model
                            currentIndex: screenChoices.indexOf(touchscreen.modelData.outputName || "")
                            onActivated: index => InputDevices.setDevice(touchscreen.modelData.sysName, "outputName", model[index].value)
                        }
                    }
                }
            }
        }
    }
}
