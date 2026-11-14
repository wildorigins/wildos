import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Taskbar behaviour and items. The panel is changed through Plasma's own
// scripting interface, so the setting takes effect at once and survives
// restarts.
PageScroll {
    id: page

    Component.onCompleted: System.readTaskbarWinModeStatus()

    readonly property var choices: [
        { mode: 0, title: "Always show the taskbar", description: "Windows stop above it" },
        { mode: 1, title: "Hide the taskbar automatically", description: "Move the pointer to the bottom of the screen to show it" },
        { mode: 2, title: "Hide it when a window covers it", description: "It stays visible until a window needs the space" }
    ]

    // Clock, battery and End task choices live in wildosrc [Taskbar]; the
    // taskbar watches the file and follows straight away.
    property int revision: 0
    function taskbarOption(key) {
        revision;
        return System.config("wildosrc", "Taskbar", key, false);
    }
    function setTaskbarOption(key, on) {
        System.setConfig("wildosrc", "Taskbar", key, on);
        revision++;
    }

    readonly property var clockOptions: [
        { key: "ClockSeconds", title: "Show seconds in the clock" },
        { key: "ClockLongDate", title: "Show the full date", description: "The day of the week and the month written out" },
        { key: "BatteryPercent", title: "Show battery percentage", description: "Beside the battery icon, on computers with a battery" }
    ]

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Taskbar behaviours"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.choices

                    UI.SettingRow {
                        id: choice
                        required property var modelData
                        readonly property bool selected: System.taskbarHiding === modelData.mode
                        title: modelData.title
                        description: modelData.description
                        interactive: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.checked: selected
                        onClicked: System.setTaskbarHiding(modelData.mode)

                        leading: UI.RadioButton {
                            checked: choice.selected
                            focusPolicy: Qt.NoFocus
                            Accessible.ignored: true
                            onClicked: System.setTaskbarHiding(choice.modelData.mode)
                        }
                    }
                }
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: "Show the desktop from the taskbar's far corner"
                    description: "Click the right end of the taskbar to show the desktop, and again to bring your windows back"
                    status: (System.taskbarShowDesktop) ? "On" : "Off"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.taskbarShowDesktop
                        Accessible.name: "Show the desktop from the taskbar's far corner"
                        onToggled: System.taskbarShowDesktop = checked
                    }
                }

                UI.SettingRow {
                    title: "Peek at the desktop"
                    description: "Rest the pointer on that corner to see the desktop until you move away"
                    status: (System.taskbarPeek) ? "On" : "Off"
                    enabled: System.taskbarShowDesktop

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.taskbarPeek
                        Accessible.name: "Peek at the desktop"
                        onToggled: System.taskbarPeek = checked
                    }
                }
            }
        }

        // Off by default, as on Windows 11: Close window lets an app ask
        // about unsaved work, End task does not.
        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: "End task"
                    description: "Adds End task to an app's right-click menu on the taskbar, to close an app that isn't responding. Anything unsaved in it is lost."
                    status: page.taskbarOption("EndTask") ? "On" : "Off"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.taskbarOption("EndTask")
                        Accessible.name: "End task"
                        onToggled: page.setTaskbarOption("EndTask", checked)
                    }
                }
            }
        }

        SectionTitle {
            text: "Taskbar items"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "windowsapps"
                    title: "Show Windows mode status on the taskbar"
                    description: "Once Windows mode is set up: whether it's running and the memory it holds"
                    status: (System.taskbarWinModeStatus) ? "On" : "Off"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.taskbarWinModeStatus
                        Accessible.name: "Show Windows mode status on the taskbar"
                        onToggled: System.setTaskbarWinModeStatus(checked)
                    }
                }

                Repeater {
                    model: page.clockOptions

                    UI.SettingRow {
                        id: optionRow
                        required property var modelData
                        title: modelData.title
                        description: modelData.description ?? ""
                        status: page.taskbarOption(modelData.key) ? "On" : "Off"

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: page.taskbarOption(optionRow.modelData.key)
                            Accessible.name: optionRow.modelData.title
                            onToggled: page.setTaskbarOption(optionRow.modelData.key, checked)
                        }
                    }
                }
            }
        }
    }
}
