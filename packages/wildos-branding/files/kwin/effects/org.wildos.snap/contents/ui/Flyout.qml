import QtQuick
import QtQuick.Window

// The picker on its own small window, for hovering the maximise button and
// for dragging a window to the top of the screen. A plain transparent window
// rather than a Plasma dialog, so the panel is Snap.dc's menu glass at
// radius 14 instead of the desktop theme's dialog frame. The window is
// larger than the panel to leave room for its shadow, except at the top,
// where it would cover the maximise button the panel hangs from.
Window {
    id: flyout

    // "hover" or "drag".
    property string mode: "hover"
    property var target: null
    property var output: null
    readonly property alias grid: panel.grid
    readonly property int sideMargin: 48
    readonly property int topMargin: 6
    readonly property int bottomMargin: 80
    readonly property int panelWidth: panel.width
    readonly property int panelHeight: panel.height

    signal zoneChosen(int layoutIndex, int zoneIndex)
    // A press in the shadow around the panel, which closes it as a press
    // outside any popup does.
    signal dismissed()

    // Read by KWin so showing the flyout never takes focus from the window
    // the person is working in.
    readonly property bool _q_showWithoutActivating: true

    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.BypassWindowManagerHint | Qt.WindowDoesNotAcceptFocus
    width: panel.width + sideMargin * 2
    height: panel.height + topMargin + bottomMargin

    // Puts the panel's top left corner at a global point.
    function place(px: real, py: real): void {
        flyout.x = Math.round(px) - sideMargin;
        flyout.y = Math.round(py) - topMargin;
    }

    // A global point inside the picker, for hit-testing while a drag holds
    // the pointer.
    function zoneAtGlobal(gx: real, gy: real): var {
        const origin = panel.grid.mapToItem(null, 0, 0);
        return panel.grid.zoneAt(gx - flyout.x - origin.x, gy - flyout.y - origin.y);
    }

    // The panel itself, without the shadow room around it.
    function globalRect(): rect {
        return Qt.rect(flyout.x + sideMargin, flyout.y + topMargin, panel.width, panel.height);
    }

    MouseArea {
        anchors.fill: parent
        onPressed: flyout.dismissed()
    }

    SnapPanel {
        id: panel
        x: flyout.sideMargin
        y: flyout.topMargin
        hint: flyout.mode === "hover" ? qsTr("Or drag a window to a screen edge.") : ""
        grid.interactive: flyout.mode === "hover"
    }

    Connections {
        target: panel.grid
        function onZoneChosen(layoutIndex: int, zoneIndex: int): void {
            flyout.zoneChosen(layoutIndex, zoneIndex);
        }
    }
}
