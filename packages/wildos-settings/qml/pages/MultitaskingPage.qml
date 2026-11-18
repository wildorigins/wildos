import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Multitasking: snapping, hot corners, Alt+Tab and the taskbar across
// desktops. The snapping and Alt+Tab settings write kwinrc and ask KWin to
// reload, so the change applies without signing out; hot corners are
// wildosrc settings KWin's wildos_hotcorners plugin follows as they change;
// the taskbar choice is the taskbar's own setting, changed through
// plasmashell.
PageScroll {
    id: page

    Component.onCompleted: System.readTaskbarDesktops()

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                title: qsTr("Double-clicking a title bar will")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Double-clicking a title bar will")
                    model: [
                        { value: "Maximize", text: qsTr("Maximise or restore") },
                        { value: "Minimize", text: qsTr("Minimise") },
                        { value: "Nothing", text: qsTr("Do nothing") }
                    ]
                    property string choice: System.config("kwinrc", "MouseBindings", "CommandTitlebarDblClick", "Maximize")
                    currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                    onActivated: index => {
                        System.setConfig("kwinrc", "MouseBindings", "CommandTitlebarDblClick", model[index].value);
                        System.reloadKWin();
                        choice = model[index].value;
                    }
                }
            }

            UI.SettingRow {
                iconName: "multitasking"
                title: qsTr("Snap windows")
                description: qsTr("Drag a window to the side of the screen to fill half of it, or to the top to maximise it")
                status: (System.snapEdges) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: System.snapEdges
                    Accessible.name: qsTr("Snap windows")
                    onToggled: System.snapEdges = checked
                }
            }

            UI.SettingRow {
                iconName: "apps"
                title: qsTr("Show snap layouts")
                description: qsTr("Hover over a window's maximise button, or press Windows key + Z, to pick a layout")
                status: (System.snapLayouts) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: System.snapLayouts
                    Accessible.name: qsTr("Show snap layouts")
                    onToggled: System.snapLayouts = checked
                }
            }

            UI.SettingRow {
                iconName: "split-right"
                title: qsTr("Suggest windows to fill the rest")
                description: qsTr("After you snap a window, pick other open windows to fill the empty spaces beside it")
                status: (System.snapAssist) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: System.snapAssist
                    Accessible.name: qsTr("Suggest windows to fill the rest")
                    onToggled: System.snapAssist = checked
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            SectionTitle {
                Layout.fillWidth: true
                text: qsTr("Hot corners")
            }

            UI.LinkButton {
                text: qsTr("Learn more")
                onClicked: Qt.openUrlExternally("https://help.wildos.io/using/hot-corners/")
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                // The Card has no padding of its own, as its rows reach its
                // edges, so the preview keeps its own distance.
                Item {
                    width: 1
                    height: 16
                }

                HotCornersPreview {
                    x: 16
                    width: parent.width - 32
                    settings: System.hotCorners
                }

                Item {
                    width: 1
                    height: 16
                }

                UI.SettingRow {
                    title: qsTr("Don't trigger in full-screen apps and games")
                    description: qsTr("Corners do nothing while the app in front fills the screen")
                    status: (System.hotCorners.SkipFullScreen) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.hotCorners.SkipFullScreen
                        Accessible.name: qsTr("Don't trigger in full-screen apps and games")
                        onToggled: System.setHotCorner("SkipFullScreen", checked)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Delay")
                    description: qsTr("How long the pointer rests in a corner before it acts")

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Repeater {
                            model: [
                                { value: 0, text: qsTr("Instant") },
                                { value: 150, text: qsTr("Short") },
                                { value: 400, text: qsTr("Long") }
                            ]

                            UI.SegmentButton {
                                required property var modelData
                                text: modelData.text
                                checked: System.hotCorners.Delay === modelData.value
                                onClicked: System.setHotCorner(qsTr("Delay"), modelData.value)
                            }
                        }
                    }
                }

                UI.SettingRow {
                    title: qsTr("Reset to defaults")
                    description: qsTr("Task view top left, notifications top right, nothing at the bottom")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Reset")
                        Accessible.name: qsTr("Reset hot corners to defaults")
                        onClicked: System.resetHotCorners()
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Desktops")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("On the taskbar, show all open windows")

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("On the taskbar, show all open windows")
                        model: [
                            { value: false, text: qsTr("Only on the desktop I'm using") },
                            { value: true, text: qsTr("On all desktops") }
                        ]
                        readonly property bool choice: System.taskbarAllDesktops
                        currentIndex: model.findIndex(o => o.value === choice)
                        onActivated: index => System.setTaskbarAllDesktops(model[index].value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Show windows from all desktops in Alt+Tab")
                    description: qsTr("When this is off, Alt+Tab only shows windows on the desktop you are using")
                    status: (System.altTabAllDesktops) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.altTabAllDesktops
                        Accessible.name: qsTr("Show windows from all desktops in Alt+Tab")
                        onToggled: System.altTabAllDesktops = checked
                    }
                }

                UI.SettingRow {
                    visible: Pages.exists("desktops")
                    iconName: "desktops"
                    title: qsTr("Desktops")
                    description: qsTr("Extra desktops for separate sets of windows")
                    chevron: true
                    onClicked: Navigator.open("desktops")
                }
            }
        }
    }

    // Task view, the desktop and switching desktops are also touchpad
    // swipes, set on their own page, which only exists with a touchpad.
    ColumnLayout {
        Layout.fillWidth: true
        visible: InputDevices.touchpads.length > 0
        spacing: 10

        SectionTitle {
            text: qsTr("Related settings")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "touchpad"
                    title: qsTr("Looking for gestures?")
                    description: qsTr("Swipe on the touchpad for Task view, the desktop and other desktops")
                    chevron: true
                    onClicked: Navigator.open("touchpadgestures")
                }
            }
        }
    }
}
