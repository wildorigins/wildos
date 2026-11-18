pragma ComponentBehavior: Bound

import QtQuick
import WildOS.Terminal
import WildOS.UI as UI

// Everything of a Terminal window that is not a terminal: the frame and
// title bar, the settings, the menus and dialogs. It is a transparent
// layer over the terminals; the window lets the pointer through to them
// except where this layer is drawing (see chromeFull).
Item {
    id: root

    readonly property var tab: host.currentTab
    readonly property bool settingsOpen: tab !== null && tab.isSettings
    readonly property bool glass: host.blurAvailable
    readonly property real radius: host.maximised ? 0 : UI.Style.windowRadius

    // While any of these is up, the whole window belongs to this layer.
    Binding {
        target: host
        property: "chromeFull"
        value: menu.opened || closeDialog.opened || pasteDialog.opened || root.settingsOpen
    }

    // The title bar's glass. Only its top corners are the window's; the
    // terminal below is drawn by the window itself.
    Rectangle {
        width: parent.width
        height: UI.Style.titleBarHeight
        topLeftRadius: root.radius
        topRightRadius: root.radius
        color: root.glass ? UI.Style.glassWindow : UI.Style.windowOpaque
    }

    TitleBar {
        id: titleBar
        width: parent.width
        onTabMenuRequested: (index, item, x, y) => root.openTabMenu(index, item, x, y)
        onProfilesMenuRequested: anchor => root.openProfilesMenu(anchor, false)
    }

    SettingsView {
        id: settings
        visible: root.settingsOpen
        transform: UI.Reveal {
            item: settings
            dy: UI.Style.travelSmall
        }
        y: UI.Style.titleBarHeight
        width: parent.width
        height: parent.height - y
        radius: root.radius
        glass: root.glass
    }

    // The window's hairline edge, over everything, as on every Wild OS
    // window.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: host.maximised ? 0 : 1
        border.color: UI.Style.windowEdge
        z: 900
    }

    function profileEntries() {
        const list = [];
        const profiles = Config.profiles;
        for (let i = 0; i < profiles.length; ++i) {
            const p = profiles[i];
            list.push({
                text: p.name,
                icon: p.icon,
                shortcut: i < 9 ? qsTr("Ctrl+Shift+%1").arg(i + 1) : "",
                run: () => host.newTab(p.id)
            });
        }
        list.push({ separator: true });
        list.push({ text: qsTr("Settings"), icon: "cog", shortcut: qsTr("Ctrl+,"), run: () => host.openSettings() });
        list.push({ text: qsTr("New window"), icon: "new-window", shortcut: qsTr("Ctrl+Shift+N"), run: () => host.newWindow() });
        return list;
    }

    function showMenu(entries, width, keyboard, item, x, y) {
        menu.minimumWidth = width;
        menu.entries = entries;
        menu.popup(item, x, y);
        // Opened from the keyboard, the first row is ready for the arrows
        // and Enter, as in Windows Terminal.
        menu.currentIndex = keyboard ? 0 : -1;
    }

    function openProfilesMenu(anchor, keyboard) {
        showMenu(profileEntries(), 280, keyboard, anchor, 0, anchor.height + 4);
    }

    function openTabMenu(index, item, x, y) {
        const t = host.tabs[index];
        showMenu([
            { text: qsTr("Duplicate tab"), icon: "duplicate", shortcut: qsTr("Ctrl+Shift+D"), enabled: !t.isSettings, run: () => host.duplicateTab(index) },
            { separator: true },
            { text: qsTr("Close tab"), icon: "close", shortcut: qsTr("Ctrl+Shift+W"), run: () => host.closeTab(index) },
            { text: qsTr("Close other tabs"), enabled: host.tabs.length > 1, run: () => host.closeOtherTabs(index) },
            { text: qsTr("Close tabs to the right"), enabled: index < host.tabs.length - 1, run: () => host.closeTabsToRight(index) }
        ], 260, false, item, x, y);
    }

    Connections {
        target: host

        function onContextMenuRequested(x, y, hasSelection, hasLink, split) {
            const entries = [];
            if (hasLink) {
                entries.push({ text: qsTr("Open link"), icon: "external", run: () => host.openLink() });
                entries.push({ text: qsTr("Copy link"), icon: "link", run: () => host.copyLink() });
                entries.push({ separator: true });
            }
            entries.push({ text: qsTr("Copy"), icon: "copy", shortcut: qsTr("Ctrl+Shift+C"), enabled: hasSelection, run: () => host.copy() });
            entries.push({ text: qsTr("Paste"), icon: "paste", shortcut: qsTr("Ctrl+Shift+V"), run: () => host.paste() });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Split right"), icon: "split-right", shortcut: qsTr("Alt+Shift+="), run: () => host.split(true) });
            entries.push({ text: qsTr("Split down"), icon: "split-down", shortcut: qsTr("Alt+Shift+-"), run: () => host.split(false) });
            if (split) {
                entries.push({ text: qsTr("Close pane"), icon: "close-pane", shortcut: qsTr("Ctrl+Shift+W"), run: () => host.closePane() });
            }
            entries.push({ separator: true });
            entries.push({ text: qsTr("New tab"), icon: "plus", shortcut: qsTr("Ctrl+Shift+T"), run: () => host.newTab("") });
            entries.push({ text: qsTr("Settings"), icon: "cog", shortcut: qsTr("Ctrl+,"), run: () => host.openSettings() });
            root.showMenu(entries, 260, false, root, x, y);
        }

        function onProfileMenuRequested() {
            root.openProfilesMenu(titleBar.profilesButton, true);
        }

        function onCloseConfirmationNeeded(count) {
            closeDialog.count = count;
            closeDialog.open();
        }

        function onPasteConfirmationNeeded(lines) {
            pasteDialog.lines = lines;
            pasteDialog.open();
        }
    }

    UI.Menu {
        id: menu
    }

    // Buttons sit at the bottom right with the main action on the right,
    // as in WinApp.dc.html; Enter takes the main action.
    UI.Dialog {
        id: closeDialog
        property int count: 0
        title: qsTr("Close all tabs?")
        text: qsTr("This window has %n tabs open. Anything still running in them will stop.", "", count)
        onClosed: host.focusTerminal()
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: closeDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Close all")
                focus: true
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                onClicked: {
                    closeDialog.close();
                    host.closeNow();
                }
            }
        ]
    }

    UI.Dialog {
        id: pasteDialog
        property int lines: 0
        property bool answered: false
        title: qsTr("Paste %n lines?", "", lines)
        text: qsTr("Each line may run as a command as soon as it is pasted. Check the text is what you meant to copy.")
        onAboutToShow: answered = false
        onClosed: {
            if (!answered) {
                host.answerPaste(false);
            }
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: pasteDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Paste anyway")
                focus: true
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                onClicked: {
                    pasteDialog.answered = true;
                    pasteDialog.close();
                    host.answerPaste(true);
                }
            }
        ]
    }

    // The frame is drawn here, so the edges that resize it are too, just
    // inside the window as on Files. UI.ResizeHandles resizes the window
    // its QML sits in, which here is the layer, not the Terminal window.
    component Grip: MouseArea {
        property int edges
        enabled: !host.maximised
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPressed: host.startResize(edges)
        z: 2000
    }

    readonly property real buttonsWidth: 3 * UI.Style.windowButtonWidth

    Grip { edges: Qt.LeftEdge; x: 0; y: 12; width: 5; height: parent.height - 24; cursorShape: Qt.SizeHorCursor }
    Grip { edges: Qt.RightEdge; x: parent.width - 5; y: 12; width: 5; height: parent.height - 24; cursorShape: Qt.SizeHorCursor }
    Grip { edges: Qt.TopEdge; x: 12; y: 0; width: parent.width - 24 - root.buttonsWidth; height: 4; cursorShape: Qt.SizeVerCursor }
    Grip { edges: Qt.BottomEdge; x: 12; y: parent.height - 5; width: parent.width - 24; height: 5; cursorShape: Qt.SizeVerCursor }
    Grip { edges: Qt.TopEdge | Qt.LeftEdge; x: 0; y: 0; width: 12; height: 12; cursorShape: Qt.SizeFDiagCursor }
    Grip { edges: Qt.TopEdge | Qt.RightEdge; x: parent.width - 6; y: 0; width: 6; height: 6; cursorShape: Qt.SizeBDiagCursor }
    Grip { edges: Qt.BottomEdge | Qt.LeftEdge; x: 0; y: parent.height - 12; width: 12; height: 12; cursorShape: Qt.SizeBDiagCursor }
    Grip { edges: Qt.BottomEdge | Qt.RightEdge; x: parent.width - 12; y: parent.height - 12; width: 12; height: 12; cursorShape: Qt.SizeFDiagCursor }
}
