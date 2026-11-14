import QtQuick
import QtQuick.Window

// Resize grips just inside the edge of a frameless window, with larger
// corners so they are easy to catch, as on Windows. The top edge stops
// short of the window buttons so it never steals a click meant for close
// (the Store's grips covered them). From wildos-files FilesWindow.
Item {
    id: handles

    property bool active: Window.window ? Window.window.visibility !== Window.Maximized && Window.window.visibility !== Window.FullScreen : false
    property real buttonsWidth: 3 * Style.windowButtonWidth
    property int edge: 5
    property int corner: 12

    anchors.fill: parent
    z: 2000

    // The last press on a grip. The first press hands the pointer to KWin
    // for the resize, so no double-click event ever arrives; a second press
    // on the same grip soon after is the double click.
    property int lastEdges: 0
    property double lastPress: 0

    component Grip: MouseArea {
        property int edges
        enabled: handles.active
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPressed: {
            const now = Date.now();
            // As on Windows: double-click the top or bottom edge for the
            // screen's full height, a side for its width.
            if (handles.lastEdges === edges && now - handles.lastPress <= Qt.styleHints.mouseDoubleClickInterval) {
                handles.lastEdges = 0;
                System.maximizeEdge(edges);
                return;
            }
            handles.lastEdges = edges;
            handles.lastPress = now;
            if (Window.window)
                Window.window.startSystemResize(edges);
        }
    }

    Grip { edges: Qt.LeftEdge; x: 0; y: handles.corner; width: handles.edge; height: parent.height - 2 * handles.corner; cursorShape: Qt.SizeHorCursor }
    Grip { edges: Qt.RightEdge; x: parent.width - handles.edge; y: handles.corner; width: handles.edge; height: parent.height - 2 * handles.corner; cursorShape: Qt.SizeHorCursor }
    Grip { edges: Qt.TopEdge; x: handles.corner; y: 0; width: parent.width - 2 * handles.corner - handles.buttonsWidth; height: 4; cursorShape: Qt.SizeVerCursor }
    Grip { edges: Qt.BottomEdge; x: handles.corner; y: parent.height - handles.edge; width: parent.width - 2 * handles.corner; height: handles.edge; cursorShape: Qt.SizeVerCursor }
    Grip { edges: Qt.TopEdge | Qt.LeftEdge; x: 0; y: 0; width: handles.corner; height: handles.corner; cursorShape: Qt.SizeFDiagCursor }
    Grip { edges: Qt.TopEdge | Qt.RightEdge; x: parent.width - 6; y: 0; width: 6; height: 6; cursorShape: Qt.SizeBDiagCursor }
    Grip { edges: Qt.BottomEdge | Qt.LeftEdge; x: 0; y: parent.height - handles.corner; width: handles.corner; height: handles.corner; cursorShape: Qt.SizeBDiagCursor }
    Grip { edges: Qt.BottomEdge | Qt.RightEdge; x: parent.width - handles.corner; y: parent.height - handles.corner; width: handles.corner; height: handles.corner; cursorShape: Qt.SizeFDiagCursor }
}
