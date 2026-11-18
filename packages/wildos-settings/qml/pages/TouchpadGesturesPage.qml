import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Touchpad gestures, one tab for each number of fingers: two inside apps,
// three and four for the system's own swipes. KWin's wildos_gestures
// effect and the apps pick up each change at once from wildosrc
// [Gestures] (the Gestures backend). Only gestures libinput reports on
// real touchpads are listed; a thumb-and-fingers pinch is not, because
// libinput takes a still thumb for a resting one and drops the gesture.
PageScroll {
    id: page

    readonly property var pad: InputDevices.touchpads.length > 0 ? InputDevices.touchpads[0] : null
    // Re-read on every change, as the backend's answers are not properties.
    property int revision: 0
    property int fingers: 3

    Connections {
        target: Gestures
        function onChanged() {
            page.revision++;
        }
    }

    readonly property var systemActions: [
        { value: "taskview", text: qsTr("Task view") },
        { value: "desktop", text: qsTr("Show desktop") },
        { value: "appgrid", text: qsTr("App grid") },
        { value: "notifications", text: qsTr("Notifications") },
        { value: "nothing", text: qsTr("Nothing") },
    ]
    // Sideways swipes move between things, so they get the actions that
    // go somewhere and back.
    readonly property var sidewaysActions: [
        { value: "desktops", text: qsTr("Switch desktops") },
        { value: "history", text: qsTr("Back and forward") },
        { value: "nothing", text: qsTr("Nothing") },
    ]
    readonly property var pageActions: [
        { value: "history", text: qsTr("Back and forward") },
        { value: "nothing", text: qsTr("Nothing") },
    ]

    component GestureRow: UI.SettingRow {
        id: gestureRow

        property string gesture
        property string kind
        property int dots: page.fingers
        property var actions: page.systemActions

        leading: GesturePreview {
            kind: gestureRow.kind
            fingers: gestureRow.dots
        }

        UI.ComboBox {
            anchors.verticalCenter: parent.verticalCenter
            width: 170
            enabled: gestureRow.enabled
            Accessible.name: gestureRow.title
            model: gestureRow.actions
            readonly property string choice: page.revision >= 0 ? Gestures.action(gestureRow.gesture) : ""
            currentIndex: model.findIndex(o => o.value === choice)
            onActivated: index => Gestures.setAction(gestureRow.gesture, model[index].value)
        }
    }

    component InfoRow: UI.SettingRow {
        id: infoRow

        property string kind

        leading: GesturePreview {
            kind: infoRow.kind
        }
    }

    StatusCard {
        visible: page.pad === null
        iconName: "touchpad"
        warning: true
        title: qsTr("No touchpad found")
        description: qsTr("This computer has no touchpad, or it is switched off in the computer's own settings.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.pad !== null
        spacing: 24

        // One tab for each number of fingers, as a pill of three choices.
        Rectangle {
            Layout.alignment: Qt.AlignLeft
            implicitWidth: tabs.implicitWidth + 8
            implicitHeight: tabs.implicitHeight + 8
            radius: UI.Style.radiusSm + 2
            color: UI.Style.surfaceTile
            border.width: 1
            border.color: UI.Style.divider

            Row {
                id: tabs
                anchors.centerIn: parent
                spacing: 4

                QQC.ButtonGroup {
                    id: tabGroup
                }

                Repeater {
                    model: [
                        { count: 2, text: qsTr("Two fingers") },
                        { count: 3, text: qsTr("Three fingers") },
                        { count: 4, text: qsTr("Four fingers") },
                    ]

                    UI.SegmentButton {
                        required property var modelData
                        text: modelData.text
                        QQC.ButtonGroup.group: tabGroup
                        checked: page.fingers === modelData.count
                        onClicked: page.fingers = modelData.count
                    }
                }
            }
        }

        // Two fingers: the gestures inside apps.
        UI.Card {
            Layout.fillWidth: true
            visible: page.fingers === 2

            Column {
                width: parent.width

                GestureRow {
                    title: qsTr("Swipe left or right")
                    description: qsTr("In Files, Settings, the Store and Music")
                    gesture: "PageSwipe"
                    kind: "page"
                    actions: page.pageActions
                }
                InfoRow {
                    title: qsTr("Scroll")
                    kind: "scroll"
                    status: qsTr("Always on")
                }
                InfoRow {
                    title: qsTr("Pinch to zoom")
                    description: qsTr("In apps that zoom, such as PDF and browsers")
                    kind: "zoom"
                    status: qsTr("Always on")
                }
                InfoRow {
                    title: qsTr("Turn to rotate")
                    description: qsTr("Pages in PDF")
                    kind: "rotate"
                    status: qsTr("Always on")
                }
                InfoRow {
                    title: qsTr("Tap to right-click")
                    description: qsTr("Works when tapping to click is on")
                    kind: "tap"
                    status: page.pad !== null && page.pad.tapToClick === true ? qsTr("On") : qsTr("Off")
                }
            }
        }

        // Three and four fingers: the system's swipes, each count its own.
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.fingers !== 2
            spacing: 8

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width
                    // Three-finger drag takes every three-finger movement.
                    readonly property bool off: page.fingers === 3 && page.revision >= 0 && Gestures.threeFingerDrag
                    readonly property string prefix: page.fingers === 3 ? "Three" : "Four"

                    GestureRow {
                        title: qsTr("Swipe up")
                        gesture: parent.prefix + "Up"
                        kind: "swipe-up"
                        enabled: !parent.off
                    }
                    GestureRow {
                        title: qsTr("Swipe down")
                        gesture: parent.prefix + "Down"
                        kind: "swipe-down"
                        enabled: !parent.off
                    }
                    GestureRow {
                        title: qsTr("Swipe left or right")
                        gesture: parent.prefix + "Sideways"
                        kind: "swipe-sideways"
                        actions: page.sidewaysActions
                        enabled: !parent.off
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: page.fingers === 3 && Gestures.threeFingerDrag
                text: qsTr("Three-finger drag is on, so three fingers drag instead of swiping.")
                size: 12
                color: UI.Style.inkMuted
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: page.fingers === 3
            spacing: 10

            SectionTitle {
                text: qsTr("Accessibility")
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        title: qsTr("Three-finger drag")
                        description: qsTr("Move windows and select text with three fingers, without clicking")
                        status: Gestures.threeFingerDrag ? qsTr("On") : qsTr("Off")
                        leading: GesturePreview {
                            kind: "drag"
                            fingers: 3
                        }

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: page.revision >= 0 && Gestures.threeFingerDrag
                            Accessible.name: qsTr("Three-finger drag")
                            onToggled: Gestures.threeFingerDrag = checked
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            UI.LinkButton {
                text: qsTr("Learn more")
                Accessible.name: qsTr("Learn more about touchpad gestures")
                onClicked: Qt.openUrlExternally("https://help.wildos.io/using/touchpad-gestures/")
            }

            Item {
                Layout.fillWidth: true
            }

            UI.Button {
                text: qsTr("Reset to defaults")
                enabled: page.revision >= 0 && !Gestures.isDefault
                onClicked: Gestures.reset()
            }
        }
    }
}
