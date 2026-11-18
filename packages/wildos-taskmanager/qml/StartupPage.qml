import QtQuick
import QtQuick.Templates as T
import WildOS.TaskManager
import WildOS.UI as UI

// Startup apps: what starts when you sign in, each with its switch, as
// Windows lists them. The list and the switches are the same as Settings >
// Apps > Startup (both write the XDG autostart override), so a change in
// either shows in the other.
Item {
    id: page

    property string search
    property string selectedFile: ""
    property string sortColumn: Prefs.sortColumn("startup", "name")
    property bool sortDescending: Prefs.sortDescending("startup")
    readonly property var apps: {
        const needle = search.trim().toLowerCase();
        return heading.sorted(StartupApps.apps.filter(a => needle.length === 0 || a.name.toLowerCase().includes(needle)));
    }
    readonly property var selected: apps.find(a => a.file === selectedFile) || null

    function sortBy(column) {
        sortDescending = sortColumn === column ? !sortDescending : false;
        sortColumn = column;
        Prefs.setSort("startup", sortColumn, sortDescending);
    }

    function rowIndex() {
        return apps.findIndex(a => a.file === selectedFile);
    }

    function selectRow(index) {
        selectedFile = apps[index].file;
    }

    function rowName(index) {
        return apps[index].name;
    }

    function toggleSelected() {
        if (selected) {
            StartupApps.setEnabled(selected.file, !selected.enabled);
        }
    }

    PageHeader {
        id: header
        width: parent.width
        title: qsTr("Startup apps")

        HeaderButton {
            text: page.selected && page.selected.enabled ? qsTr("Disable") : qsTr("Enable")
            iconName: page.selected && page.selected.enabled ? "close" : "check"
            enabled: page.selected !== null
            onClicked: page.toggleSelected()
        }
    }

    TableHeading {
        id: heading
        y: header.height
        width: parent.width
        columns: [
            { id: "name", title: qsTr("Name"), width: 0 },
            { id: "enabled", title: qsTr("Status"), width: 120, value: app => app.enabled ? 0 : 1 },
            { title: "", width: 84 }
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
        Accessible.name: qsTr("Startup apps")
        T.ScrollBar.vertical: UI.ScrollBar {}

        Keys.onPressed: event => listKeys.handle(event)
        Keys.onSpacePressed: page.toggleSelected()
        Keys.onReturnPressed: page.toggleSelected()
        Keys.onEnterPressed: page.toggleSelected()

        delegate: TableRow {
            id: row
            required property var modelData
            width: list.width
            selected: modelData.file === page.selectedFile
            focused: selected && listKeys.shown
            Accessible.name: modelData.name + ", " + (modelData.enabled ? qsTr("Enabled") : qsTr("Disabled"))
            onClicked: {
                page.selectedFile = modelData.file;
                listKeys.clicked();
            }

            Item {
                width: row.width - 120 - 84
                height: row.height

                AppIcon {
                    x: 16
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    icon: row.modelData.icon
                }

                Column {
                    x: 16 + 20 + 12
                    width: parent.width - x - 12
                    anchors.verticalCenter: parent.verticalCenter

                    UI.Label {
                        width: parent.width
                        text: row.modelData.name
                    }

                    UI.Label {
                        visible: text.length > 0
                        width: parent.width
                        text: row.modelData.description
                        size: 11
                        color: UI.Style.inkMuted
                    }
                }
            }

            UI.Label {
                width: 120
                height: row.height
                leftPadding: 12
                text: row.modelData.enabled ? qsTr("Enabled") : qsTr("Disabled")
                size: 12
                color: UI.Style.inkMuted
            }

            Item {
                width: 84
                height: row.height

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    checked: row.modelData.enabled
                    Accessible.name: qsTr("Start %1 when you sign in").arg(row.modelData.name)
                    onToggled: StartupApps.setEnabled(row.modelData.file, checked)
                }
            }
        }
    }

    UI.Label {
        visible: list.count === 0
        anchors.centerIn: list
        width: Math.min(420, list.width - 48)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: page.search.length > 0 ? qsTr("No startup app matches “%1”.").arg(page.search) : qsTr("No apps start when you sign in. Apps you set to start with Wild OS show here.")
        color: UI.Style.inkMuted
    }

    ListKeys {
        id: listKeys
        view: list
        host: page
        rowHeight: 44
    }
}
