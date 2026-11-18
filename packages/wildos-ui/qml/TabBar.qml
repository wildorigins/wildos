import QtQuick
import QtQuick.Templates as T

// The tabs of a title bar: TabButtons share the room left of the window
// buttons (72 to 220 each, 4 apart) with a 32px + after the last, which
// emits newTabRequested. From wildos-files TitleBar. Sits on the bottom of
// a 42px TitleBar so the current tab joins the bar below it.
//
// With `reorderable`, dragging a tab sideways past its neighbour emits
// moved(from, to) each time it changes place; the app moves the entry in
// its own list and keeps the same tab current. A right click or the Menu
// key on a tab emits contextMenuRequested with the tab's index and where
// to open the menu, in the tab bar's coordinates.
T.TabBar {
    id: control

    property bool showNewTab: true
    property string newTabTip: qsTr("New tab (Ctrl+T)")
    property real room: width
    readonly property real tabWidth: Math.max(72, Math.min(220,
        (room - (showNewTab ? 40 : 0) - (count - 1) * spacing) / Math.max(1, count)))

    property bool reorderable: false

    signal newTabRequested()
    signal moved(int from, int to)
    signal contextMenuRequested(int index, real x, real y)

    function tabAt(x: real): int {
        return Math.max(0, Math.min(count - 1, Math.floor(x / (tabWidth + spacing))));
    }

    implicitWidth: count * tabWidth + (count - 1) * spacing + (showNewTab ? 40 : 0)
    implicitHeight: 34
    spacing: 4
    padding: 0

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        orientation: ListView.Horizontal
        spacing: control.spacing
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        implicitWidth: contentWidth
    }

    background: Item {
        // Only as wide as the tabs, so the empty bar beside them still
        // moves the window.
        Item {
            width: control.count * (control.tabWidth + control.spacing) - control.spacing
            height: parent.height

            DragHandler {
                id: reorder

                property int from: -1

                enabled: control.reorderable && control.count > 1
                target: null
                acceptedButtons: Qt.LeftButton
                yAxis.enabled: false
                onActiveChanged: from = active ? control.tabAt(centroid.pressPosition.x) : -1
                // Tabs are all one width, so the dragged tab is under the
                // pointer again as soon as the app has moved it.
                onCentroidChanged: {
                    if (!active || from < 0) {
                        return;
                    }
                    const to = control.tabAt(centroid.position.x);
                    if (to !== from) {
                        const was = from;
                        from = to;
                        control.moved(was, to);
                    }
                }
            }
        }

        IconButton {
            visible: control.showNewTab
            x: control.count * (control.tabWidth + control.spacing)
            y: control.height - height - 2
            width: 32
            height: 32
            iconName: "plus"
            iconSize: 14
            stroke: 2
            tip: control.newTabTip
            onClicked: control.newTabRequested()
        }
    }
}
