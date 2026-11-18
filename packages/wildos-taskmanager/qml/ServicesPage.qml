import QtQuick
import QtQuick.Templates as T
import WildOS.TaskManager
import WildOS.UI as UI

// Services: what the system and your account run in the background, by
// plain name, with whether each is running and how it starts. Start, Stop
// and Restart act on the selected one; a system service asks for an
// administrator's password first. "Show all" adds the parts of the system
// nobody normally needs to touch.
Item {
    id: page

    // Services gives the status in English, which this page compares;
    // listed here for the translations and shown with qsTr().
    readonly property var statusNames: [QT_TR_NOOP("Running"), QT_TR_NOOP("Starting"), QT_TR_NOOP("Stopping"), QT_TR_NOOP("Failed"), QT_TR_NOOP("Stopped")]

    property string selectedKey: ""
    property string sortColumn: Prefs.sortColumn("services", "name")
    property bool sortDescending: Prefs.sortDescending("services")
    readonly property var rows: heading.sorted(Services.services)
    readonly property var selected: rows.find(s => s.key === selectedKey) || null

    signal message(string text)

    function act(action) {
        if (page.selected) {
            Services.act(page.selected.unit, page.selected.user, action);
        }
    }

    function sortBy(column) {
        sortDescending = sortColumn === column ? !sortDescending : false;
        sortColumn = column;
        Prefs.setSort("services", sortColumn, sortDescending);
    }

    function rowIndex() {
        return rows.findIndex(s => s.key === selectedKey);
    }

    function selectRow(index) {
        selectedKey = rows[index].key;
    }

    function rowName(index) {
        return rows[index].name;
    }

    function showMenu(service, item, at) {
        menu.entries = [
            { text: qsTr("Start"), icon: "play", enabled: !service.running && !service.masked, run: () => page.act("start") },
            { text: qsTr("Stop"), icon: "close", enabled: service.running, run: () => page.act("stop") },
            { text: qsTr("Restart"), icon: "restart", enabled: service.running, run: () => page.act("restart") }
        ];
        menu.popup(item, at.x, at.y);
    }

    Connections {
        target: Services
        function onFailed(text) {
            page.message(text);
        }
    }

    PageHeader {
        id: header
        width: parent.width
        title: qsTr("Services")

        Row {
            height: 32
            spacing: 10

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Show all")
                size: 12
            }

            UI.Toggle {
                anchors.verticalCenter: parent.verticalCenter
                checked: Services.showAll
                Accessible.name: qsTr("Show all services")
                onToggled: Services.showAll = checked
            }

            Item {
                width: 6
                height: 1
            }
        }

        HeaderButton {
            text: qsTr("Start")
            iconName: "play"
            enabled: page.selected !== null && !page.selected.running && !page.selected.masked
            onClicked: page.act("start")
        }

        HeaderButton {
            text: qsTr("Restart")
            iconName: "restart"
            enabled: page.selected !== null && page.selected.running
            onClicked: page.act("restart")
        }

        HeaderButton {
            kind: "primary"
            text: qsTr("Stop")
            iconName: "close"
            enabled: page.selected !== null && page.selected.running
            onClicked: page.act("stop")
        }
    }

    TableHeading {
        id: heading
        y: header.height
        width: parent.width
        columns: [
            { id: "name", title: qsTr("Name"), width: 0 },
            { id: "unit", title: qsTr("Service"), width: 220 },
            { id: "status", title: qsTr("Status"), width: 110 },
            { id: "starts", title: qsTr("Starts"), width: 130 },
            { id: "owner", title: qsTr("Runs for"), width: 90 }
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
        model: page.rows
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        activeFocusOnTab: true
        keyNavigationEnabled: false
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Services")
        T.ScrollBar.vertical: UI.ScrollBar {}

        Keys.onPressed: event => listKeys.handle(event)
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
            implicitHeight: 40
            selected: modelData.key === page.selectedKey
            focused: selected && listKeys.shown
            Accessible.name: modelData.name + ", " + qsTr(modelData.status)
            onClicked: {
                page.selectedKey = modelData.key;
                listKeys.clicked();
            }
            onMenuRequested: at => page.showMenu(row.modelData, row, at)

            UI.Label {
                width: row.width - 220 - 110 - 130 - 90
                height: row.height
                leftPadding: 16
                rightPadding: 12
                text: row.modelData.name
            }

            UI.Label {
                width: 220
                height: row.height
                leftPadding: 12
                rightPadding: 12
                text: row.modelData.unit
                size: 12
                color: UI.Style.inkMuted
            }

            UI.Label {
                width: 110
                height: row.height
                leftPadding: 12
                text: qsTr(row.modelData.status)
                size: 12
                color: row.modelData.status === "Failed" ? UI.Style.danger : UI.Style.inkMuted
            }

            UI.Label {
                width: 130
                height: row.height
                leftPadding: 12
                text: row.modelData.starts
                size: 12
                color: UI.Style.inkMuted
            }

            UI.Label {
                width: 90
                height: row.height
                leftPadding: 12
                text: row.modelData.owner
                size: 12
                color: UI.Style.inkMuted
            }
        }
    }

    UI.Label {
        visible: list.count === 0
        anchors.centerIn: list
        text: Services.search.length > 0 ? qsTr("No service matches “%1”.").arg(Services.search) : qsTr("Wild OS could not read the list of services.")
        color: UI.Style.inkMuted
    }

    ListKeys {
        id: listKeys
        view: list
        host: page
        rowMenu: menu
    }

    UI.Menu {
        id: menu
    }
}
