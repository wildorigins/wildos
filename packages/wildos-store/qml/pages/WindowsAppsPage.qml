import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// Windows apps: for the programs that have no free swap, running the
// Windows program itself, set up from Settings.
PageFlick {
    id: page

    property var store
    property string arg
    property string kind

    PageTitle {
        title: qsTr("Windows apps")
        subtitle: qsTr("For the programs nothing here replaces, Wild OS can run the Windows version.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        SectionHeader {
            title: qsTr("Run a Windows program")
        }

        WindowsModeRow {
            title: qsTr("Install the Windows program you already have")
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        SectionHeader {
            title: qsTr("Or swap it for a free app")
            linkText: qsTr("See all")
            onLinkClicked: page.store.open("swap")
        }

        AppList {
            keys: Catalog.catalogRevision >= 0 ? Catalog.swaps() : []
            maxItems: 6
            showNotes: true
            onActivated: key => page.store.openApp(key)
        }
    }
}
