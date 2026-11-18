import QtQuick
import QtQuick.Window

// The 42px title bar of a Wild OS app that draws its own (Files, Settings,
// Store, Terminal), or the 44px one of the apps drawn with a taller bar
// (`tall: true`, which sizes the window buttons to match): an optional
// Back button (Settings.dc, 34 by 30, radius
// 6), the app's title in 13px, anything the app puts in the middle (tabs,
// a search box) and the window buttons at the right. Dragging the empty
// bar moves the window once the pointer has travelled 4px, so the second
// click of a double click (maximise) still lands here; a middle click is
// passed on (Files opens a new tab), and a right click opens the window
// menu, as a right click on any other title bar does (MenusShell.dc). From wildos-files TitleBar and
// wildos-settings Main.qml.
Item {
    id: bar

    property string title
    property bool showTitle: title.length > 0
    property bool showBack: false
    property bool tall: false
    property bool backEnabled: true
    // Files tints its bar; Settings' bar is the window glass itself.
    property bool tinted: false
    property var frame: null
    property alias buttons: windowButtons
    // False when the bar is not drawn in the window it moves, as in
    // Terminal, whose QML is a layer over a widget window: dragging and
    // double-clicking then ask through moveRequested and maximiseRequested,
    // and the window buttons only emit their signals.
    property bool handleActions: true
    default property alias content: middle.data
    readonly property bool maximised: windowButtons.maximised

    signal backClicked()
    signal middleClicked()
    signal moveRequested()
    signal maximiseRequested()
    signal windowMenuRequested()

    implicitHeight: tall ? Style.titleBarHeightTall : Style.titleBarHeight

    Rectangle {
        anchors.fill: parent
        visible: bar.tinted
        topLeftRadius: bar.maximised ? 0 : Style.windowRadius - 1
        topRightRadius: topLeftRadius
        // A neutral shade on Mica: black, not navy, so the title bar keeps
        // the window's own tint.
        color: Style.dark ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(0, 0, 0, 0.03)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        property point origin
        property bool moving: false
        onPressed: mouse => {
            origin = Qt.point(mouse.x, mouse.y);
            moving = false;
        }
        onPositionChanged: mouse => {
            if (pressed && pressedButtons === Qt.LeftButton && !moving && Math.abs(mouse.x - origin.x) + Math.abs(mouse.y - origin.y) > 4) {
                moving = true;
                if (!bar.handleActions) {
                    bar.moveRequested();
                } else if (bar.Window.window) {
                    bar.Window.window.startSystemMove();
                }
            }
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton && windowButtons.showMaximise) {
                if (!bar.handleActions) {
                    bar.maximiseRequested();
                } else if (bar.frame) {
                    bar.frame.toggleMaximised();
                } else if (bar.maximised) {
                    bar.Window.window.showNormal();
                } else {
                    bar.Window.window.showMaximized();
                }
            }
        }
        onClicked: mouse => {
            if (mouse.button === Qt.MiddleButton) {
                bar.middleClicked();
            } else if (mouse.button === Qt.RightButton) {
                if (bar.handleActions && System.hasWindowMenu()) {
                    System.showWindowMenu();
                } else {
                    bar.windowMenuRequested();
                }
            }
        }
    }

    Row {
        id: lead
        x: bar.showBack ? 10 : 16
        height: parent.height
        spacing: 8

        IconButton {
            visible: bar.showBack
            anchors.verticalCenter: parent.verticalCenter
            width: 34
            height: 30
            radius: Style.radiusXs
            iconName: "back"
            iconSize: 15
            enabled: bar.backEnabled
            tip: qsTr("Back")
            onClicked: bar.backClicked()
        }

        Label {
            visible: bar.showTitle
            anchors.verticalCenter: parent.verticalCenter
            text: bar.title
            color: bar.Window.active ? Style.ink : Style.inkMuted
        }
    }

    Item {
        id: middle
        anchors.left: lead.right
        anchors.leftMargin: lead.width > 0 ? 8 : 0
        anchors.right: windowButtons.left
        anchors.rightMargin: 8
        height: parent.height
    }

    WindowButtons {
        id: windowButtons
        anchors.right: parent.right
        anchors.top: parent.top
        frame: bar.frame
        handleActions: bar.handleActions
        buttonHeight: bar.implicitHeight
    }
}
