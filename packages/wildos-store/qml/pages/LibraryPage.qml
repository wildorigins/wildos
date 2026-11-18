import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// Library: every app installed from either catalogue, to open, update or
// uninstall from its menu. The box at the top narrows the list to a name,
// and the list runs by name or with the largest app first.
AppGrid {
    id: page

    property var store
    property string arg
    property string kind

    property string filter
    property string sort: "name"
    readonly property var installed: Catalog.catalogRevision + Catalog.stateRevision >= 0 ? Catalog.installedApps() : []

    keys: {
        const needle = filter.trim().toLowerCase();
        let list = installed;
        if (needle.length > 0) {
            list = list.filter(key => {
                const app = Catalog.app(key);
                return app !== null && app.name.toLowerCase().includes(needle);
            });
        }
        if (sort === "size") {
            // Apps of the same size, or of none known, stay in name order.
            const rows = list.map((key, place) => ({ key: key, place: place, size: Catalog.installedSize(key) }));
            rows.sort((a, b) => b.size !== a.size ? b.size - a.size : a.place - b.place);
            list = rows.map(row => row.key);
        }
        return list;
    }
    showMore: true
    showSizes: sort === "size"
    onActivated: key => store.openApp(key)

    header: Column {
        width: page.innerWidth
        spacing: 18
        bottomPadding: 4

        PageTitle {
            width: parent.width
            title: qsTr("Library")
            subtitle: !Catalog.ready ? qsTr("Getting the list of apps")
                : page.installed.length === 0 ? qsTr("No apps to show yet.")
                : page.keys.length === 0 ? qsTr("No app matches “%1”.").arg(page.filter.trim())
                : page.keys.length === 1 ? qsTr("1 app") : qsTr("%1 apps").arg(page.keys.length)

            UI.SearchField {
                visible: page.installed.length > 0
                width: 220
                height: Math.max(32, implicitHeight - 4)
                placeholderText: qsTr("Search your apps")
                onTextChanged: page.filter = text
            }

            FilterPill {
                visible: page.installed.length > 0
                text: qsTr("By name")
                selected: page.sort === "name"
                onClicked: page.sort = "name"
            }

            FilterPill {
                visible: page.installed.length > 0
                text: qsTr("By size")
                selected: page.sort === "size"
                onClicked: page.sort = "size"
            }
        }

        // What the sidebar's ring stands for: apps on their way in or out.
        SectionHeader {
            visible: working.keys.length > 0
            width: parent.width
            title: qsTr("Getting now")
        }

        AppList {
            id: working
            visible: keys.length > 0
            width: parent.width
            keys: Catalog.stateRevision >= 0 ? Catalog.workingApps() : []
            onActivated: key => page.store.openApp(key)
        }

        Rectangle {
            visible: page.keys.length > 0
            width: parent.width
            height: 1
            color: Metrics.divider
        }
    }
}
