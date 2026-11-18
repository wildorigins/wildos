import QtQuick
import QtQuick.Layouts
import WildOS.TaskManager
import WildOS.UI as UI

// The Task Manager window as TaskManager.dc.html draws it: one glass sheet,
// 1140 by 690, with a 46px title bar (icon, "Task Manager", the search box
// in the middle, window buttons), the page list on the left and the page
// on the right under its own 54px header. KWin's snap layouts find the
// maximise button by the app id org.wildos.taskmanager (46px bar).
UI.AppWindow {
    id: window

    required property string requestedPage
    property string currentPage: ""
    readonly property var pages: [
        { id: "processes", title: qsTr("Processes"), icon: "processes" },
        { id: "performance", title: qsTr("Performance"), icon: "performance" },
        { id: "startup", title: qsTr("Startup apps"), icon: "startup-apps" },
        { id: "windowsapps", title: qsTr("Windows apps"), icon: "monitor" },
        { id: "services", title: qsTr("Services"), icon: "services" }
    ]
    readonly property var placeholders: ({
        processes: qsTr("Search for a name or process"),
        performance: qsTr("Search for a name or process"),
        startup: qsTr("Search for an app"),
        windowsapps: qsTr("Search for an app"),
        services: qsTr("Search for a service"),
        settings: qsTr("Search for a name or process")
    })

    width: 1140
    height: 690
    minimumWidth: 860
    minimumHeight: 520
    visible: true
    title: qsTr("Task Manager")
    deep: true

    function showPage(id) {
        const known = pages.some(p => p.id === id) || id === "settings";
        currentPage = known ? id : "processes";
        searchField.text = "";
    }

    function toast(message) {
        if (message && message.length > 0) {
            toastItem.show(message);
        }
    }

    Component.onCompleted: {
        const column = Prefs.sortColumn("processes", "name");
        if (["name", "status", "cpu", "memory", "disk", "network"].includes(column)) {
            // Read before the column is set: setting it saves the sort, with
            // the direction as it stands, over the one saved last time.
            const descending = Prefs.sortDescending("processes");
            Processes.sortColumn = column;
            Processes.sortDescending = descending;
        }
        showPage(requestedPage.length > 0 ? requestedPage : Prefs.startPage);
    }

    Connections {
        target: Processes
        function onSortChanged() {
            Prefs.setSort("processes", Processes.sortColumn, Processes.sortDescending);
        }
    }

    // The numbers update at the speed chosen in Task Manager's settings;
    // "Paused" stops both the list and the graphs, as in Windows.
    Binding {
        target: SystemStats
        property: "interval"
        value: Prefs.updateInterval
    }
    Binding {
        target: SystemStats
        property: "paused"
        value: Prefs.updateSpeed === "paused"
    }
    Binding {
        target: Processes
        property: "paused"
        value: Prefs.updateSpeed === "paused"
    }
    Binding {
        target: Processes
        property: "showSystem"
        value: Prefs.showSystem
    }
    Binding {
        target: Processes
        property: "search"
        value: window.currentPage === "processes" ? searchField.text : ""
    }
    Binding {
        target: Services
        property: "search"
        value: window.currentPage === "services" ? searchField.text : ""
    }
    Binding {
        target: Services
        property: "active"
        value: window.currentPage === "services" && window.active
    }

    Shortcut {
        sequences: [StandardKey.Find, "Ctrl+E"]
        onActivated: searchField.forceActiveFocus()
    }

    Shortcut {
        sequence: "F5"
        onActivated: {
            Services.reload();
            StartupApps.reload();
        }
    }

    UI.TitleBar {
        id: titleBar
        width: parent.width
        height: 46
        frame: window.frame
        buttons.buttonHeight: 46

        Row {
            x: -2
            height: parent.height
            spacing: 12

            UI.ThemeIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                name: "org.wildos.taskmanager"
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Task Manager")
                font.weight: UI.Style.weightMedium
                color: window.active ? UI.Style.ink : UI.Style.inkMuted
            }
        }

        UI.SearchField {
            id: searchField
            // Centred in the space between the title and the window
            // buttons, as the artboard's two equal spacers put it.
            x: Math.max(130, (114 + parent.width - width) / 2)
            width: Math.min(320, parent.width - 150)
            height: 30
            anchors.verticalCenter: parent.verticalCenter
            pill: true
            iconSize: 14
            iconInset: 12
            leftPadding: 34
            font.pixelSize: UI.Style.px(12)
            visible: window.currentPage !== "performance" && window.currentPage !== "settings"
            placeholderText: window.placeholders[window.currentPage] || ""
            onEscapePressed: window.contentItem.forceActiveFocus()
        }
    }

    Rectangle {
        y: titleBar.height
        width: parent.width
        height: 1
        color: UI.Style.divider
    }

    Item {
        id: body
        y: titleBar.height + 1
        width: parent.width
        height: parent.height - y

        Column {
            id: nav
            x: 10
            y: 12
            width: 210
            spacing: 2

            Repeater {
                model: window.pages

                NavItem {
                    required property var modelData
                    width: nav.width
                    text: modelData.title
                    iconName: modelData.icon
                    current: window.currentPage === modelData.id
                    onClicked: window.showPage(modelData.id)
                }
            }
        }

        NavItem {
            x: 10
            y: parent.height - height - 12
            width: nav.width
            text: qsTr("Settings")
            iconName: "options"
            current: window.currentPage === "settings"
            onClicked: window.showPage("settings")
        }

        Rectangle {
            x: 230
            width: 1
            height: parent.height
            color: UI.Style.divider
        }

        Item {
            id: content
            x: 231
            width: parent.width - x
            height: parent.height
            clip: true
            // Each page rises a little as it fades in, as Windows Task
            // Manager does.
            transform: UI.Reveal {
                item: content
                key: window.currentPage
                dy: UI.Style.travelMedium
            }

            Loader {
                anchors.fill: parent
                active: window.currentPage === "processes"
                visible: active
                sourceComponent: ProcessesPage {
                    onMessage: text => window.toast(text)
                    onRunTask: runDialog.open()
                }
            }

            Loader {
                anchors.fill: parent
                active: window.currentPage === "performance"
                visible: active
                sourceComponent: PerformancePage {
                    onMessage: text => window.toast(text)
                    onRunTask: runDialog.open()
                }
            }

            Loader {
                anchors.fill: parent
                active: window.currentPage === "startup"
                visible: active
                sourceComponent: StartupPage {
                    search: searchField.text
                }
            }

            Loader {
                anchors.fill: parent
                active: window.currentPage === "windowsapps"
                visible: active
                sourceComponent: WindowsAppsPage {
                    search: searchField.text
                }
            }

            Loader {
                anchors.fill: parent
                active: window.currentPage === "services"
                visible: active
                sourceComponent: ServicesPage {
                    onMessage: text => window.toast(text)
                }
            }

            Loader {
                anchors.fill: parent
                active: window.currentPage === "settings"
                visible: active
                sourceComponent: SettingsPage {}
            }
        }
    }

    UI.Toast {
        id: toastItem
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: 105
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }

    RunDialog {
        id: runDialog
    }
}
