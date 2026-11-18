import QtQuick
import QtQuick.Layouts
import WildOS.TaskManager
import WildOS.UI as UI

// Task Manager's own settings, as Windows has them: the page it opens on,
// how often the numbers update (or paused), and whether the system's own
// processes are listed.
Item {
    id: page

    readonly property var startPages: [
        { value: "processes", text: qsTr("Processes") },
        { value: "performance", text: qsTr("Performance") },
        { value: "startup", text: qsTr("Startup apps") },
        { value: "windowsapps", text: qsTr("Windows apps") },
        { value: "services", text: qsTr("Services") }
    ]
    readonly property var speeds: [
        { value: "high", text: qsTr("High (twice a second)") },
        { value: "normal", text: qsTr("Normal (every second)") },
        { value: "low", text: qsTr("Low (every 4 seconds)") },
        { value: "paused", text: qsTr("Paused") }
    ]

    PageHeader {
        id: header
        width: parent.width
        title: qsTr("Settings")
    }

    Column {
        x: 22
        y: header.height + 20
        width: Math.min(parent.width - 44, 720)
        spacing: 4

        UI.Card {
            width: parent.width
            implicitHeight: cardColumn.implicitHeight

            Column {
                id: cardColumn
                width: parent.width

                UI.SettingRow {
                    width: parent.width
                    iconName: "processes"
                    title: qsTr("Default start page")
                    description: qsTr("The page Task Manager opens on")

                    UI.ComboBox {
                        width: 200
                        Accessible.name: qsTr("Default start page")
                        model: page.startPages
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: Math.max(0, page.startPages.findIndex(p => p.value === Prefs.startPage))
                        onActivated: Prefs.startPage = currentValue
                    }
                }

                UI.SettingRow {
                    width: parent.width
                    iconName: "refresh"
                    title: qsTr("Update speed")
                    description: qsTr("How often the numbers and graphs change")

                    UI.ComboBox {
                        width: 220
                        Accessible.name: qsTr("Update speed")
                        model: page.speeds
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: Math.max(0, page.speeds.findIndex(s => s.value === Prefs.updateSpeed))
                        onActivated: Prefs.updateSpeed = currentValue
                    }
                }

                UI.SettingRow {
                    width: parent.width
                    iconName: "system"
                    title: qsTr("Show Wild OS processes")
                    description: qsTr("List what the system runs for itself under Processes")

                    UI.Toggle {
                        checked: Prefs.showSystem
                        Accessible.name: qsTr("Show Wild OS processes")
                        onToggled: Prefs.showSystem = checked
                    }
                }
            }
        }
    }
}
