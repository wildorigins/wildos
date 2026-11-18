import QtQuick
import QtQuick.Window
import WildOS.UI as UI

// The window menu (MenusShell.dc "Window title bar") in a clear window over
// the screen, so its submenus have room. The window is a pop-up to KWin:
// it gets the keyboard, and a press outside it closes it.
Window {
    id: root

    property QtObject controller: null

    flags: Qt.Popup | Qt.FramelessWindowHint
    color: "transparent"

    function openMenu(state, px, py) {
        const run = (action, argument) => () => root.controller.act(action, argument);
        const entries = [];
        if (state.canMinimise) {
            entries.push({ text: qsTr("Minimise"), icon: "minus", run: run("minimise") });
        }
        if (state.canMaximise) {
            entries.push({ text: state.maximised ? qsTr("Restore") : qsTr("Maximise"), icon: state.maximised ? "win-restore" : "menu-maximise", run: run("maximise") });
        }
        if (state.canSnap) {
            entries.push({ text: qsTr("Snap"), icon: "menu-snap", submenuWidth: 200, submenu: [
                { text: qsTr("Left half"), run: run("snap", "left") },
                { text: qsTr("Right half"), run: run("snap", "right") },
                { separator: true },
                { text: qsTr("Top left quarter"), run: run("snap", "top-left") },
                { text: qsTr("Top right quarter"), run: run("snap", "top-right") },
                { text: qsTr("Bottom left quarter"), run: run("snap", "bottom-left") },
                { text: qsTr("Bottom right quarter"), run: run("snap", "bottom-right") }
            ] });
        }
        if (state.canChangeDesktop) {
            const desktops = state.desktops.map((desktop, index) => ({
                text: desktop.name, checked: desktop.current, run: run("desktop", index)
            }));
            desktops.push({ separator: true }, { text: qsTr("New desktop"), icon: "plus", run: run("desktop", -1) });
            entries.push({ text: qsTr("Move to desktop"), icon: "menu-desktop", submenu: desktops, submenuWidth: 200 });
        }
        if (entries.length > 0) {
            entries.push({ separator: true });
        }
        entries.push({ text: qsTr("Keep on top"), icon: "menu-front", checked: state.keepAbove, run: run("keepAbove") });
        if (state.canMove) {
            entries.push({ text: qsTr("Move"), icon: "menu-move", run: run("move") });
        }
        if (state.canClose) {
            entries.push({ separator: true }, { text: qsTr("Close"), icon: "close", shortcut: "Alt+F4", run: run("close") });
        }
        menu.entries = entries;
        visible = true;
        menu.popup(px, py);
    }

    // A press on the clear part of the window is a press outside the menu.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: menu.close()
    }

    UI.Menu {
        id: menu
        minimumWidth: 280
        maximumWidth: 280
        shadow: false
        onClosed: root.visible = false
    }
}
