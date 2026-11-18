import QtQuick
import QtQuick.Window
import WildOS.Files
import WildOS.UI as UI

// A Files window. It has no system title bar: the tabs live in its own,
// so the Wild OS app window supplies the glass, shadow, blur and resize
// grips the Aurorae decoration would otherwise give it.
UI.AppWindow {
    id: window

    property var startUrls: []
    property string formatUdi: ""
    // The size to come back to from maximised, which is also the size the
    // next window opens at.
    property size restored: Settings.windowSize()

    // Files keeps its own size and maximised state in its settings.
    rememberSize: false
    width: Settings.windowSize().width
    height: Settings.windowSize().height
    // A window closed maximised opens maximised, as on Windows.
    visibility: Settings.windowMaximised() ? Window.Maximized : Window.Windowed
    title: view.title

    onActiveChanged: if (active) FileOps.activeWindow = window
    onWidthChanged: sizeSettled.restart()
    onHeightChanged: sizeSettled.restart()
    onClosing: {
        if (visibility === Window.Windowed) {
            restored = Qt.size(width, height);
        }
        Settings.setWindowSize(restored);
        Settings.setWindowMaximised(maximised);
        view.saveTabs();
    }
    Component.onCompleted: FileOps.activeWindow = window

    // The size changes a moment before or after the window reports that it
    // is maximised, depending on the compositor, so the size is only taken
    // as the one to come back to once both have settled.
    Timer {
        id: sizeSettled
        interval: 250
        onTriggered: {
            if (window.visibility === Window.Windowed) {
                window.restored = Qt.size(window.width, window.height);
            }
        }
    }

    FilesView {
        id: view
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        startUrls: window.startUrls
        formatUdi: window.formatUdi
        frame: window.frame
        onCloseWindow: window.close()
    }
}
