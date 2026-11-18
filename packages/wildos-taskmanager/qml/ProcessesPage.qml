import QtQuick
import QtQuick.Templates as T
import WildOS.TaskManager
import WildOS.UI as UI

// Processes (TaskManager.dc): "Run new task" and "End task" in the header,
// then the table, its column headings carrying the whole computer's use
// (CPU, memory, the Wild OS disk's active time, the network's share of the
// connection), grouped into Apps, Background processes and Wild OS
// processes. Select a row and press End task or Delete; right-click for
// the rest.
Item {
    id: page

    // Processes gives endLabel in English, which this page compares;
    // listed here for the translations and shown with qsTr().
    readonly property var endLabels: [QT_TR_NOOP("Restart"), QT_TR_NOOP("End task")]

    property string selectedKey: ""
    // Read again whenever the list updates.
    readonly property var selected: Processes.processCount >= 0 && selectedKey.length > 0 ? Processes.entry(selectedKey) : ({})
    readonly property bool canEnd: selected.key !== undefined && selected.canEnd === true

    signal message(string text)
    signal runTask()

    function end(key, tree) {
        const info = Processes.entry(key);
        if (!info.key || !info.canEnd) {
            return;
        }
        // The system's own processes can take part of Wild OS with them.
        if (info.group === 2) {
            confirm.key = key;
            confirm.tree = tree;
            confirm.title = qsTr("End %1?").arg(info.name);
            confirm.open();
            return;
        }
        page.message(tree ? Processes.endTree(key) : Processes.endTask(key));
    }

    function showMenu(key, item, at) {
        const info = Processes.entry(key);
        if (!info.key) {
            return;
        }
        const entries = [];
        if (info.expandable) {
            entries.push({ text: info.expanded ? qsTr("Collapse") : qsTr("Expand"), run: () => Processes.toggle(key) });
            entries.push({ separator: true });
        }
        entries.push({ text: qsTr(info.endLabel), icon: info.endLabel === "Restart" ? "restart" : "close", enabled: info.canEnd, run: () => page.end(key, false) });
        if (!info.winMode) {
            entries.push({ text: qsTr("End process tree"), enabled: info.canEnd, run: () => page.end(key, true) });
        }
        if (!info.winMode && info.single) {
            const levels = [[qsTr("High"), -10], [qsTr("Above normal"), -5], [qsTr("Normal"), 0], [qsTr("Below normal"), 5], [qsTr("Low"), 19]];
            entries.push({ separator: true });
            entries.push({
                text: qsTr("Set priority"),
                submenu: levels.map(l => ({ text: l[0], run: () => page.message(Processes.setPriority(key, l[1])) }))
            });
            entries.push({ text: qsTr("Open file location"), icon: "open-folder", run: () => Processes.openFileLocation(key) });
        }
        if (info.winMode) {
            entries.push({ separator: true });
            entries.push({ text: qsTr("Windows app settings"), icon: "options", run: () => Prefs.openSettings("windowsapps") });
        }
        menu.entries = entries;
        menu.popup(item, at.x, at.y);
    }

    Keys.onDeletePressed: if (page.canEnd) page.end(page.selectedKey, false)

    function rowIndex() {
        return Processes.indexOf(selectedKey);
    }

    function selectRow(index) {
        selectedKey = Processes.keyAt(index);
    }

    function rowName(index) {
        return Processes.entry(Processes.keyAt(index)).name || "";
    }

    function rowSelectable(index) {
        return !Processes.isHeading(index);
    }

    PageHeader {
        id: header
        width: parent.width
        title: qsTr("Processes")

        HeaderButton {
            text: qsTr("Run new task")
            iconName: "plus"
            onClicked: page.runTask()
        }

        HeaderButton {
            kind: "primary"
            text: qsTr(page.selected.endLabel || QT_TR_NOOP("End task"))
            iconName: page.selected.endLabel === "Restart" ? "restart" : "close"
            enabled: page.canEnd
            onClicked: page.end(page.selectedKey, false)
        }
    }

    Item {
        id: columns
        y: header.height
        width: parent.width
        height: 53

        ColumnHeader {
            width: parent.width - 120 - 4 * 92
            height: parent.height - 1
            label: qsTr("Name")
            alignRight: false
            sorted: Processes.sortColumn === "name"
            descending: Processes.sortDescending
            onClicked: Processes.sortBy("name")

            // The design's Name heading sits on the middle line on its own.
            contentItem: Item {
                UI.Label {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Name")
                    size: 12
                    color: UI.Style.inkAlpha(0.7, 0.64)
                }
                UI.Icon {
                    visible: Processes.sortColumn === "name"
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 3
                    name: Processes.sortDescending ? "chevron-down" : "chevron-up"
                    size: 10
                    strokeWidth: 2
                    color: UI.Style.iconMuted
                }
            }
        }

        Row {
            anchors.right: parent.right
            height: parent.height

            ColumnHeader {
                width: 120
                height: parent.height - 1
                label: qsTr("Status")
                alignRight: false
                sorted: Processes.sortColumn === "status"
                descending: Processes.sortDescending
                onClicked: Processes.sortBy("status")
            }

            Repeater {
                model: [
                    { id: "cpu", label: qsTr("CPU"), total: () => Format.percent(SystemStats.cpuUsage) },
                    { id: "memory", label: qsTr("Memory"), total: () => Format.percent(SystemStats.memoryPercent) },
                    { id: "disk", label: qsTr("Disk"), total: () => Format.percent(SystemStats.diskPercent) },
                    { id: "network", label: qsTr("Network"), total: () => Format.percent(SystemStats.networkPercent) }
                ]

                ColumnHeader {
                    required property var modelData
                    width: 92
                    height: parent.height - 1
                    label: modelData.label
                    total: SystemStats.revision >= 0 ? modelData.total() : ""
                    sorted: Processes.sortColumn === modelData.id
                    descending: Processes.sortDescending
                    onClicked: Processes.sortBy(modelData.id)
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: UI.Style.tone(0.08)
        }
    }

    ListView {
        id: list
        y: columns.y + columns.height
        width: parent.width
        height: parent.height - y
        clip: true
        model: Processes
        boundsBehavior: Flickable.StopAtBounds
        focus: true
        activeFocusOnTab: true
        keyNavigationEnabled: false
        reuseItems: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Processes")
        T.ScrollBar.vertical: UI.ScrollBar {}
        // The first list takes a moment to gather; it fades in when it is
        // there rather than appearing all at once on an empty page.
        opacity: Processes.ready ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionNormal; easing.type: Easing.OutCubic }
        }

        delegate: ProcessRow {
            width: list.width
            selected: key === page.selectedKey
            focused: selected && listKeys.shown
            memoryTotal: SystemStats.memoryTotal
            onChosen: k => {
                page.selectedKey = k;
                listKeys.clicked();
            }
            onExpandToggled: k => Processes.toggle(k)
            onMenuRequested: (k, at) => page.showMenu(k, this, at)
        }

        Keys.onPressed: event => listKeys.handle(event)
        Keys.onRightPressed: if (page.selected.expandable) Processes.setExpanded(page.selectedKey, true)
        Keys.onLeftPressed: if (page.selected.expandable) Processes.setExpanded(page.selectedKey, false)
        Keys.onReturnPressed: if (page.selected.expandable) Processes.toggle(page.selectedKey)
        Keys.onMenuPressed: {
            const i = Processes.indexOf(page.selectedKey);
            const item = itemAtIndex(i);
            if (item) {
                page.showMenu(page.selectedKey, item, Qt.point(40, item.height));
            }
        }
    }

    UI.Label {
        visible: Processes.ready && list.count === 0
        anchors.centerIn: list
        text: Processes.search.length > 0 ? qsTr("Nothing running matches “%1”.").arg(Processes.search) : ""
        color: UI.Style.inkMuted
    }

    ListKeys {
        id: listKeys
        view: list
        host: page
        rowMenu: menu
        rowHeight: 37
    }

    UI.Menu {
        id: menu
    }

    UI.Dialog {
        id: confirm
        property string key
        property bool tree: false
        text: qsTr("Wild OS uses this to run. Ending it can make part of the system stop working until you restart.")

        buttons: [
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("End process")
                onClicked: {
                    confirm.close();
                    page.message(confirm.tree ? Processes.endTree(confirm.key) : Processes.endTask(confirm.key));
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: confirm.close()
            }
        ]
    }
}
