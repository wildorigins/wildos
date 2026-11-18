import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// Every app or every game in the catalogue, narrowed by category, from
// Categories.
AppGrid {
    id: page

    property var store
    property string arg
    property string kind
    readonly property bool games: kind === "games"
    readonly property string categoryId: arg.length > 0 ? arg : (games ? "games" : "all")

    keys: Catalog.catalogRevision >= 0 ? Catalog.category(categoryId) : []
    onActivated: key => store.openApp(key)

    header: Column {
        width: page.innerWidth
        spacing: 18
        bottomPadding: 4

        PageTitle {
            width: parent.width
            title: page.arg.length > 0 ? Catalog.categoryLabel(page.arg) : page.games ? qsTr("Games") : qsTr("All apps")
            subtitle: !Catalog.ready ? qsTr("Getting the list of apps")
                : page.keys.length === 1 ? (page.games ? qsTr("1 game") : qsTr("1 app")) : (page.games ? qsTr("%1 games") : qsTr("%1 apps")).arg(page.keys.length)
        }

        Flow {
            width: parent.width
            spacing: 8

            FilterPill {
                text: page.games ? qsTr("All games") : qsTr("All apps")
                selected: page.arg.length === 0
                onClicked: page.store.open(page.kind, "")
            }

            Repeater {
                model: page.games ? Catalog.gameCategories : Catalog.appCategories

                FilterPill {
                    required property var modelData
                    text: modelData.label
                    selected: page.arg === modelData.id
                    onClicked: page.store.open(page.kind, modelData.id)
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Metrics.divider
        }
    }
}
