import QtQuick
import QtQuick.Templates as T
import WildOS.Music
import WildOS.UI as UI

// A page of covers (Albums, Artists): as many columns as fit, each tile
// stretched to share the row, under the page's heading.
GridView {
    id: grid

    property Component heading
    property bool round: false
    property real minimumTile: 160

    signal opened(var item)
    signal played(var item)
    signal menuAt(var item, Item tile, real x, real y)

    readonly property real usable: width - 56
    readonly property int columns: Math.max(1, Math.floor((usable + 24) / (minimumTile + 24)))
    // Each cell carries a 24px gap at its right; the right margin is cut by
    // the same so the last gap sits in it.
    readonly property real tileSize: cellWidth - 24

    clip: true
    focus: true
    leftMargin: 28
    rightMargin: 4
    bottomMargin: 24
    cellWidth: Math.floor((usable + 24) / columns)
    cellHeight: tileSize + 74
    boundsBehavior: Flickable.StopAtBounds
    T.ScrollBar.vertical: UI.ScrollBar {}

    // The view places its header itself; the loader inside is moved back
    // over the left margin so the heading spans the whole page.
    header: Item {
        width: grid.width - grid.leftMargin - grid.rightMargin
        height: headingLoader.height

        Loader {
            id: headingLoader
            x: -grid.leftMargin
            width: grid.width
            sourceComponent: grid.heading
        }
    }

    delegate: Item {
        id: cell
        required property var modelData
        required property int index
        width: grid.cellWidth
        height: grid.cellHeight

        AlbumTile {
            id: tile
            // Tab can reach a cover that is scrolled out of sight.
            onActiveFocusChanged: if (activeFocus) grid.positionViewAtIndex(cell.index, GridView.Contain)
            coverSize: grid.tileSize
            round: grid.round
            placeholder: grid.round ? "artist" : "album"
            cover: cell.modelData.cover ?? ""
            title: grid.round ? cell.modelData.name : cell.modelData.title
            subtitle: grid.round ? MusicStyle.songs(cell.modelData.count) : cell.modelData.artist
            onClicked: grid.opened(cell.modelData)
            onPlayClicked: grid.played(cell.modelData)
            onMenuRequested: (x, y) => grid.menuAt(cell.modelData, tile, x, y)
        }
    }
}
