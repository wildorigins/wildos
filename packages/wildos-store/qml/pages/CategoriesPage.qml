import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Store
import WildOS.UI as UI

// Every category of apps and of games, each opening the full list.
PageFlick {
    id: page

    property var store
    property string arg
    property string kind

    component CategoryTile: T.AbstractButton {
        id: tile
        property string iconName
        implicitHeight: Math.max(56, Math.ceil(tileLabel.implicitHeight) + 24)
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.Button
        Accessible.name: text

        background: Rectangle {
            radius: Metrics.radiusCard
            // Fades in on hover and clears at once, so moving along a list
            // never lights two items. It comes before the colour so it sees
            // the change in hover first.
            Behavior on color {
                enabled: tile.hovered
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
            color: tile.down ? Metrics.pill : tile.hovered ? Metrics.buttonFill : Metrics.card

            UI.FocusRing {
                visible: tile.visualFocus
                targetRadius: Metrics.radiusCard
            }
        }

        contentItem: Item {
            Rectangle {
                id: well
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                radius: 9
                color: UI.Style.accentAlpha(0.13, 0.11)

                StoreIcon {
                    anchors.centerIn: parent
                    name: tile.iconName
                    size: 18
                    color: UI.Style.accent
                }
            }

            UI.Label {
                id: tileLabel
                anchors.left: well.right
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: tile.text
                size: 14.5
                font.weight: UI.Style.weightSemiBold
            }
        }
    }

    component TileGrid: Grid {
        Layout.fillWidth: true
        columns: width > 900 ? 4 : width > 560 ? 3 : 2
        columnSpacing: 12
        rowSpacing: 12
        readonly property real cellWidth: (width - columnSpacing * (columns - 1)) / columns
    }

    readonly property var appIcons: ({
        office: "document", internet: "globe", graphics: "reach-image", media: "videos",
        education: "info", development: "develop", utilities: "box", system: "cog"
    })

    PageTitle {
        title: qsTr("Categories")
        subtitle: qsTr("Every app and game in the Store, by what it does.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        SectionHeader {
            title: qsTr("Apps")
        }

        TileGrid {
            id: appGrid

            CategoryTile {
                width: appGrid.cellWidth
                iconName: "categories"
                text: qsTr("All apps")
                onClicked: page.store.open("apps", "")
            }

            Repeater {
                model: Catalog.appCategories

                CategoryTile {
                    required property var modelData
                    width: appGrid.cellWidth
                    iconName: page.appIcons[modelData.id] || "categories"
                    text: modelData.label
                    onClicked: page.store.open("apps", modelData.id)
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        SectionHeader {
            title: qsTr("Games")
        }

        TileGrid {
            id: gameGrid

            CategoryTile {
                width: gameGrid.cellWidth
                iconName: "play"
                text: qsTr("All games")
                onClicked: page.store.open("games", "")
            }

            Repeater {
                model: Catalog.gameCategories

                CategoryTile {
                    required property var modelData
                    width: gameGrid.cellWidth
                    iconName: "play"
                    text: modelData.label
                    onClicked: page.store.open("games", modelData.id)
                }
            }
        }
    }
}
