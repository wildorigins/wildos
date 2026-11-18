import QtQuick
import QtQuick.Templates as T
import WildOS.TaskManager
import WildOS.UI as UI

// Windows apps: every app the App Bridge installed, whether its Windows
// mode session is running, suspended (idle, frozen so it costs nothing) or
// not running, and what it uses now. Open starts it as Start does, and
// Settings opens Settings > Windows apps, where its permissions and removal
// live. There is no End task: Windows mode can only end a program by force,
// which loses unsaved work, so an app is closed from its own window.
Item {
    id: page

    property string search
    property string selectedId: ""
    property string sortColumn: Prefs.sortColumn("windowsapps", "name")
    property bool sortDescending: Prefs.sortDescending("windowsapps")
    readonly property var apps: {
        const needle = search.trim().toLowerCase();
        return heading.sorted(WindowsApps.apps.filter(a => needle.length === 0 || a.name.toLowerCase().includes(needle) || a.publisher.toLowerCase().includes(needle)));
    }
    readonly property var selected: apps.find(a => a.id === selectedId) || null

    function sortBy(column) {
        sortDescending = sortColumn === column ? !sortDescending : false;
        sortColumn = column;
        Prefs.setSort("windowsapps", sortColumn, sortDescending);
    }

    function rowIndex() {
        return apps.findIndex(a => a.id === selectedId);
    }

    function selectRow(index) {
        selectedId = apps[index].id;
    }

    function rowName(index) {
        return apps[index].name;
    }

    function showMenu(app, item, at) {
        menu.entries = [
            { text: qsTr("Open"), icon: "open", run: () => WindowsApps.open(app.id) },
            { separator: true },
            { text: qsTr("Windows app settings"), icon: "options", run: () => WindowsApps.openSettings() }
        ];
        menu.popup(item, at.x, at.y);
    }

    PageHeader {
        id: header
        width: parent.width
        title: qsTr("Windows apps")

        HeaderButton {
            text: qsTr("Settings")
            iconName: "options"
            onClicked: WindowsApps.openSettings()
        }

        HeaderButton {
            text: qsTr("Open")
            iconName: "open"
            enabled: page.selected !== null
            onClicked: WindowsApps.open(page.selected.id)
        }
    }

    TableHeading {
        id: heading
        y: header.height
        width: parent.width
        // Publisher, CPU and Memory are the same for every app (they share
        // one Windows), so there is nothing to sort by.
        columns: [
            { id: "name", title: qsTr("Name"), width: 0 },
            { title: qsTr("Publisher"), width: 200 },
            { id: "running", title: qsTr("Status"), width: 120, value: app => app.running ? 0 : 1 },
            { title: qsTr("CPU"), width: 92, right: true },
            { title: qsTr("Memory"), width: 92, right: true }
        ]
        sortColumn: page.sortColumn
        sortDescending: page.sortDescending
        onSortRequested: column => page.sortBy(column)
    }

    ListView {
        id: list
        y: heading.y + heading.height
        width: parent.width
        height: parent.height - y
        clip: true
        model: page.apps
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: false
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Windows apps")
        T.ScrollBar.vertical: UI.ScrollBar {}

        Keys.onPressed: event => listKeys.handle(event)
        Keys.onReturnPressed: if (page.selected) WindowsApps.open(page.selected.id)
        Keys.onEnterPressed: if (page.selected) WindowsApps.open(page.selected.id)
        Keys.onMenuPressed: {
            const item = itemAtIndex(page.rowIndex());
            if (item && page.selected) {
                page.showMenu(page.selected, item, Qt.point(40, item.height));
            }
        }

        delegate: TableRow {
            id: row
            required property var modelData
            width: list.width
            selected: modelData.id === page.selectedId
            focused: selected && listKeys.shown
            Accessible.name: modelData.name + ", " + modelData.status
            onClicked: {
                page.selectedId = modelData.id;
                listKeys.clicked();
            }
            onDoubleClicked: WindowsApps.open(modelData.id)
            onMenuRequested: at => page.showMenu(row.modelData, row, at)

            Item {
                width: row.width - 200 - 120 - 2 * 92
                height: row.height

                AppIcon {
                    x: 16
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    icon: row.modelData.icon
                }

                UI.Label {
                    x: 16 + 20 + 12
                    width: parent.width - x - 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.name
                }
            }

            UI.Label {
                width: 200
                height: row.height
                leftPadding: 12
                rightPadding: 12
                text: row.modelData.publisher
                size: 12
                color: UI.Style.inkMuted
            }

            UI.Label {
                width: 120
                height: row.height
                leftPadding: 12
                text: row.modelData.status
                size: 12
                color: UI.Style.inkMuted
            }

            Rectangle {
                width: 92
                height: row.height - 1
                color: Colours.heatColour(Colours.heat("cpu", row.modelData.cpu, 1))

                UI.Label {
                    anchors.fill: parent
                    anchors.rightMargin: 12
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.running ? Format.cpu(row.modelData.cpu) : ""
                    size: 12
                    font.features: { "tnum": 1 }
                }
            }

            Rectangle {
                width: 92
                height: row.height - 1
                color: Colours.heatColour(Colours.heat("memory", row.modelData.memory, SystemStats.memoryTotal))

                UI.Label {
                    anchors.fill: parent
                    anchors.rightMargin: 12
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.running ? Format.memory(row.modelData.memory) : ""
                    size: 12
                    font.features: { "tnum": 1 }
                }
            }
        }
    }

    UI.Label {
        visible: list.count === 0
        anchors.centerIn: list
        width: Math.min(440, list.width - 48)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: page.search.length > 0 ? qsTr("No Windows app matches “%1”.").arg(page.search) : qsTr("No Windows apps yet. Open a Windows installer (.exe or .msi) in Files to add one.")
        color: UI.Style.inkMuted
    }

    ListKeys {
        id: listKeys
        view: list
        host: page
        rowMenu: menu
        rowHeight: 44
    }

    UI.Menu {
        id: menu
    }
}
