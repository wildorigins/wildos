pragma ComponentBehavior: Bound

import QtQuick
import WildOS.Terminal
import WildOS.UI as UI

// The 42px title bar with the tabs in it, built like Files': a tab per
// shell (and one for the settings), + for another, the profiles menu
// beside it as in Windows Terminal, the window buttons on the right, and
// the empty part for dragging the window. The QML is a layer over a widget
// window, so moving, maximising and the window buttons go through the
// host window rather than the layer's own.
UI.TitleBar {
    id: bar

    signal tabMenuRequested(int index, Item tab, real x, real y)
    signal profilesMenuRequested(Item anchor)

    property alias profilesButton: profilesButton

    tinted: true
    handleActions: false
    buttons.maximised: host.maximised
    buttons.windowActive: host.windowActive
    onMoveRequested: host.startMove()
    onMaximiseRequested: host.toggleMaximised()
    onMiddleClicked: host.newTab("")

    Connections {
        target: bar.buttons
        function onMinimiseClicked() {
            host.minimise();
        }
        function onMaximiseClicked() {
            host.toggleMaximised();
        }
        function onCloseClicked() {
            host.requestClose();
        }
    }

    UI.TabBar {
        id: tabs

        // As in Files: the first tab sits 8px from the window's edge, closer
        // than a title would.
        x: -8
        anchors.bottom: parent.bottom
        width: implicitWidth
        // Leaves some bar free to drag the window by, however many tabs.
        room: parent.width + 8 - profilesButton.width - 4 - 48
        newTabTip: qsTr("New tab (Ctrl+Shift+T)")
        currentIndex: host.currentIndex
        onNewTabRequested: host.newTab("")
        reorderable: true
        onMoved: (from, to) => host.moveTab(from, to)

        Repeater {
            model: host.tabs

            delegate: UI.TabButton {
                id: tab

                required property var modelData
                required property int index

                text: modelData.title
                // The settings tab's gear is "cog" in the shared set.
                iconName: modelData.iconName === "settings" ? "cog" : modelData.iconName
                checked: index === host.currentIndex
                onPressed: host.currentIndex = index
                onCloseRequested: host.closeTab(index)
                onContextMenuRequested: (x, y) => bar.tabMenuRequested(tab.index, tab, x, y)
            }
        }
    }

    UI.IconButton {
        id: profilesButton
        x: tabs.x + tabs.width
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: 28
        height: 32
        iconName: "chevron-down"
        iconSize: 12
        stroke: 2.2
        tip: qsTr("Profiles and settings (Ctrl+Shift+Space)")
        focusPolicy: Qt.NoFocus
        onClicked: bar.profilesMenuRequested(profilesButton)
    }
}
