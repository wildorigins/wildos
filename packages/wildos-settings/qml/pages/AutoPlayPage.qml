import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// AutoPlay, after Windows 11: one switch for every drive and memory card,
// then when it applies, and the drives this PC has seen. "Ready" here is
// what Plasma's automounter does: the drive is mounted and shows in Files
// without a click. Nothing on a drive is ever run.
PageScroll {
    id: page

    readonly property bool on: AutoPlay.onPlugin || AutoPlay.onLogin

    Component.onCompleted: AutoPlay.reload()

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "play"
                title: qsTr("Use AutoPlay for all media and devices")
                description: qsTr("Drives and memory cards are ready in Files as soon as you plug them in")
                status: page.on ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.on
                    Accessible.name: qsTr("Use AutoPlay for all media and devices")
                    onToggled: {
                        AutoPlay.setOnPlugin(checked);
                        AutoPlay.setOnLogin(checked);
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.on
        spacing: 10

        SectionTitle {
            text: qsTr("When to use AutoPlay")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("When you plug in a drive or memory card")
                    status: AutoPlay.onPlugin ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: AutoPlay.onPlugin
                        Accessible.name: qsTr("When you plug in a drive or memory card")
                        onToggled: AutoPlay.setOnPlugin(checked)
                    }
                }

                UI.SettingRow {
                    title: qsTr("When you sign in")
                    description: qsTr("For drives that are already plugged in")
                    status: AutoPlay.onLogin ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: AutoPlay.onLogin
                        Accessible.name: qsTr("When you sign in")
                        onToggled: AutoPlay.setOnLogin(checked)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Include drives this PC hasn't seen before")
                    description: qsTr("Otherwise only drives you've opened here before are made ready")
                    status: AutoPlay.unknownDevices ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: AutoPlay.unknownDevices
                        Accessible.name: qsTr("Include drives this PC hasn't seen before")
                        onToggled: AutoPlay.setUnknownDevices(checked)
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Drives")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    id: driveList
                    model: AutoPlay.drives

                    UI.SettingRow {
                        id: driveRow
                        required property var modelData
                        iconName: "usb"
                        title: modelData.name

                        UI.ComboBox {
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("AutoPlay for %1").arg(driveRow.modelData.name)
                            model: [{ value: false, text: qsTr("Use the settings above") }, { value: true, text: qsTr("Always") }]
                            readonly property bool choice: driveRow.modelData.always === true
                            currentIndex: model.findIndex(o => o.value === choice)
                            onActivated: index => AutoPlay.setAlways(driveRow.modelData.udi, model[index].value)
                        }

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Forget")
                            onClicked: AutoPlay.forget(driveRow.modelData.udi)
                        }
                    }
                }

                UI.SettingRow {
                    visible: driveList.count === 0
                    title: qsTr("No drives yet")
                    description: qsTr("Drives and memory cards you plug in appear here.")
                }
            }
        }
    }
}
