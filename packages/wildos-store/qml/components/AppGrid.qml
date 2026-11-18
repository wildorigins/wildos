import QtQuick
import QtQuick.Controls
import WildOS.Store
import WildOS.UI as UI

// A long list of apps in the boards' three columns, for categories and
// search results that can run to thousands: only the rows in view are
// made, and they are reused as the list scrolls. The page's title and
// anything above the list go in `header`.
GridView {
    id: grid

    property var keys: []
    // Rows get a button for the app's menu (Library, where Uninstall is).
    property bool showMore: false
    // Rows say what the app takes up instead of what it is (Library, when
    // it is sorted by size).
    property bool showSizes: false
    // The header's width, inside the page's 44px sides.
    readonly property real innerWidth: width - 2 * Metrics.sidePadding

    signal activated(string key)

    readonly property int columns: innerWidth > 900 ? 3 : innerWidth > 560 ? 2 : 1
    readonly property int lastRowStart: {
        const n = keys.length;
        const rem = n % columns;
        return n - (rem === 0 ? columns : rem);
    }
    leftMargin: Metrics.sidePadding
    // The last column's gap hangs into the right margin.
    rightMargin: Metrics.sidePadding - Metrics.columnGap
    topMargin: Metrics.pageTop
    bottomMargin: 32
    cellWidth: Math.floor((width - leftMargin - rightMargin) / columns)
    cellHeight: probe.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    model: keys
    reuseItems: true
    ScrollBar.vertical: UI.ScrollBar {}

    function keyPressed(event) {
        scrollKeys.handle(event);
    }

    ScrollKeys {
        id: scrollKeys
        view: grid
    }

    // Every cell is as tall as one row at the current text size.
    AppListRow {
        id: probe
        visible: false
        showMore: grid.showMore
        width: 300
    }

    delegate: Item {
        id: cell
        required property string modelData
        required property int index
        width: grid.cellWidth
        height: grid.cellHeight

        AppListRow {
            width: parent.width - Metrics.columnGap
            height: parent.height
            appKey: cell.modelData
            divider: cell.index < grid.lastRowStart
            showMore: grid.showMore
            subtitle: !app ? "" : grid.showSizes && app.sizeText.length > 0 ? app.sizeText : app.tagline
            onActivated: key => grid.activated(key)
        }
    }
}
