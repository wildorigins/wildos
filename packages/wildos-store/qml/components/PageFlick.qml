import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WildOS.UI as UI

// The scrolling body of a page, with the boards' padding (4px top, 44px
// each side) and 26px between sections.
Flickable {
    id: flick

    default property alias content: column.data
    property alias spacing: column.spacing

    contentWidth: width
    contentHeight: column.implicitHeight + Metrics.pageTop + 32
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: UI.ScrollBar {}

    function keyPressed(event) {
        scrollKeys.handle(event);
    }

    ScrollKeys {
        id: scrollKeys
        view: flick
    }

    ColumnLayout {
        id: column
        x: Metrics.sidePadding
        y: Metrics.pageTop
        width: flick.width - 2 * Metrics.sidePadding
        spacing: Metrics.sectionGap
    }
}
