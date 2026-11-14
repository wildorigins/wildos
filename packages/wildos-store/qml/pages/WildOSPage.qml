import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// Wild OS (StoreWildOS.dc.html): every app we make, from the curated
// file's "wildos" list, each with Open or Reinstall, and Reinstall all
// beside the title while any were removed.
PageFlick {
    id: page

    property var store
    property string arg
    property string kind

    readonly property var keys: Catalog.catalogRevision >= 0 ? Catalog.wildosApps() : []
    readonly property var apps: keys.map(k => Catalog.app(k)).filter(a => !!a)
    // Read again whenever an app's state changes.
    readonly property int revision: Catalog.stateRevision
    readonly property var putting: revision >= 0 ? apps.filter(a => (a.busy || a.queued) && !a.installed && a.action !== "remove") : []
    readonly property var removed: revision >= 0 ? apps.filter(a => !a.installed && !a.busy && !a.queued) : []

    PageTitle {
        title: "Wild OS"
        subtitle: "The apps we make. They come with Wild OS, cost nothing and need no account."

        PillButton {
            visible: page.removed.length > 0 || page.putting.length > 0
            big: true
            filled: true
            enabled: page.removed.length > 0 && page.putting.length === 0
            text: "Reinstall all"
            onClicked: Catalog.reinstallWildOS()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        SectionHeader {
            title: "All " + page.apps.length + " apps"
        }

        Grid {
            id: grid
            Layout.fillWidth: true
            columns: width > 640 ? 2 : 1
            columnSpacing: Metrics.columnGap
            readonly property real cellWidth: (width - columnSpacing * (columns - 1)) / columns
            // Down each column, as the board fills them: the first half on
            // the left.
            flow: Grid.TopToBottom
            rows: Math.ceil(page.keys.length / columns)

            Repeater {
                model: page.keys

                WildOSRow {
                    required property string modelData
                    required property int index
                    width: grid.cellWidth
                    appKey: modelData
                    divider: (index + 1) % grid.rows !== 0 && index !== page.keys.length - 1
                    onActivated: key => page.store.openApp(key)
                }
            }
        }
    }
}
