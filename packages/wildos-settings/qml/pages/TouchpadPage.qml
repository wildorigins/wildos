import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Touchpad, after Windows 11: on or off, speed, taps, scrolling and a
// link to gestures. KWin applies each change straight away and keeps it
// for the touchpad.
PageScroll {
    id: page

    readonly property var pad: InputDevices.touchpads.length > 0 ? InputDevices.touchpads[0] : null

    function has(key) {
        return pad !== null && pad[key] === true;
    }

    StatusCard {
        visible: page.pad === null
        iconName: "touchpad"
        warning: true
        title: qsTr("No touchpad found")
        description: qsTr("This computer has no touchpad, or it is switched off in the computer's own settings.")
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.pad !== null

        Column {
            width: parent.width

            UI.SettingRow {
                visible: page.has("supportsDisableEvents")
                iconName: "touchpad"
                title: qsTr("Touchpad")
                status: (page.pad !== null && page.pad.enabled !== false) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.pad !== null && page.pad.enabled !== false
                    Accessible.name: qsTr("Touchpad")
                    onToggled: InputDevices.set("touchpad", "enabled", checked)
                }
            }

            UI.SettingRow {
                visible: page.has("supportsDisableEventsOnExternalMouse")
                title: qsTr("Leave touchpad on when a mouse is connected")
                status: (page.pad !== null && page.pad.disableEventsOnExternalMouse !== true) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.pad !== null && page.pad.disableEventsOnExternalMouse !== true
                    Accessible.name: qsTr("Leave touchpad on when a mouse is connected")
                    onToggled: InputDevices.set("touchpad", "disableEventsOnExternalMouse", !checked)
                }
            }

            UI.SettingRow {
                id: speedRow
                visible: page.has("supportsPointerAcceleration")
                title: qsTr("Cursor speed")
                readonly property real acceleration: page.pad ? (page.pad.pointerAcceleration || 0) : 0

                UI.Slider {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 240
                    accessibleName: qsTr("Cursor speed")
                    from: -1
                    to: 1
                    stepSize: 0.1
                    value: speedRow.acceleration
                    onMoved: InputDevices.set("touchpad", "pointerAcceleration", Math.round(value * 10) / 10)
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.pad !== null
        spacing: 10

        SectionTitle {
            text: qsTr("Taps and scrolling")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    visible: page.pad !== null && page.pad.tapFingerCount > 0
                    title: qsTr("Tap with a single finger to single-click")
                    status: (page.pad !== null && page.pad.tapToClick === true) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.pad !== null && page.pad.tapToClick === true
                        Accessible.name: qsTr("Tap with a single finger to single-click")
                        onToggled: InputDevices.set("touchpad", "tapToClick", checked)
                    }
                }

                UI.SettingRow {
                    visible: page.has("supportsDisableWhileTyping")
                    title: qsTr("Ignore the touchpad while typing")
                    status: (page.pad !== null && page.pad.disableWhileTyping === true) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.pad !== null && page.pad.disableWhileTyping === true
                        Accessible.name: qsTr("Ignore the touchpad while typing")
                        onToggled: InputDevices.set("touchpad", "disableWhileTyping", checked)
                    }
                }

                UI.SettingRow {
                    visible: page.has("supportsNaturalScroll")
                    title: qsTr("Scrolling follows your fingers")
                    description: qsTr("Drag two fingers up to scroll down, as on a phone")
                    status: (page.pad !== null && page.pad.naturalScroll === true) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.pad !== null && page.pad.naturalScroll === true
                        Accessible.name: qsTr("Scrolling follows your fingers")
                        onToggled: InputDevices.set("touchpad", "naturalScroll", checked)
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.pad !== null

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "touchpad"
                title: qsTr("Gestures")
                description: qsTr("Swipes with two, three or four fingers")
                chevron: true
                onClicked: Navigator.open("touchpadgestures")
            }
        }
    }
}
