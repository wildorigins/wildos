pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.Remote
import WildOS.UI as UI

// Everything of the terminal window that isn't a terminal (RemoteTerminal.dc,
// RemoteFiles.dc): the tabs in the title bar, the snippets panel, the
// status bar, the suggestions under the cursor and the files view. It is a
// transparent layer over the terminals, which the window lets the pointer
// through to wherever this layer isn't drawing.
Item {
    id: root

    readonly property var tab: host.currentTab
    readonly property bool glass: host.blurAvailable
    readonly property real radius: host.maximised ? 0 : UI.Style.windowRadius
    readonly property rect area: host.terminalArea
    // The bars are the window's own Mica, so they follow light and dark
    // like every other window; without blur they have to be solid.
    readonly property color barColour: glass ? UI.Style.glassWindow : UI.Style.windowOpaque
    property bool runAll: false

    // While any of these is up, the whole window belongs to this layer.
    Binding {
        target: host
        property: "chromeFull"
        value: tabMenu.opened || terminalMenu.opened || plusMenu.opened || runAllDialog.opened || forwardMenu.opened || signInDialog.opened || questionDialog.opened || pasteDialog.opened
    }

    // The terminal's ssh asking for a password, a code or a passphrase:
    // asked here, over the tab it's for, not in the home window.
    Connections {
        target: AskPass
        function onPassphraseNeeded(id, title, prompt, kind, context, details) {
            if (context !== "terminal") {
                return;
            }
            const tabs = host.tabs;
            for (let i = 0; i < tabs.length; ++i) {
                if (details.hostId && tabs[i].hostId === details.hostId) {
                    host.currentIndex = i;
                    break;
                }
            }
            host.focusChrome();
            if (kind === "password" && details.save) {
                signInDialog.ask(id, details);
            } else {
                questionDialog.ask(id, title, prompt, kind);
            }
        }
        function onQuestionClosed(id) {
            signInDialog.closeFor(id);
            questionDialog.closeFor(id);
        }
    }

    // The suggestions under the cursor, and the part of the window they
    // take from the terminal.
    readonly property bool suggesting: tab !== null && tab.suggestions.length > 0 && !tab.filesOpen
    Binding {
        target: host
        property: "overlay"
        value: root.suggesting ? Qt.rect(suggestions.x, suggestions.y, suggestions.width, suggestions.height) : Qt.rect(0, 0, 0, 0)
    }

    // The title bar and status bar glass; the terminal area between them is
    // drawn by the window in the terminal's own colour.
    Rectangle {
        width: parent.width
        height: 46
        topLeftRadius: root.radius
        topRightRadius: root.radius
        color: root.barColour
    }
    Rectangle {
        y: parent.height - 32
        width: parent.width
        height: 32
        bottomLeftRadius: root.radius
        bottomRightRadius: root.radius
        color: root.barColour
        Rectangle {
            width: parent.width
            height: 1
            color: UI.Style.tone(0.06)
        }
    }

    // Title bar: moving the window from any empty part of it.
    MouseArea {
        width: parent.width
        height: 46
        property point origin
        onPressed: m => origin = Qt.point(m.x, m.y)
        onPositionChanged: m => {
            if (pressed && Math.abs(m.x - origin.x) + Math.abs(m.y - origin.y) > 4) {
                host.startMove();
            }
        }
        onDoubleClicked: host.toggleMaximised()
    }

    Image {
        x: 16
        y: 13
        width: 20
        height: 20
        source: "qrc:/qt/qml/WildOS/Remote/data/org.wildos.remote.svg"
        sourceSize: Qt.size(40, 40)
        MouseArea {
            id: homeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: host.showHome()
        }
        UI.Tooltip {
            text: qsTr("Your hosts")
            visible: homeMouse.containsMouse
        }
    }

    Row {
        id: tabRow
        x: 50
        y: 7
        height: 32
        spacing: 4
        Repeater {
            model: host.tabs
            Rectangle {
                id: tabItem
                required property var modelData
                required property int index
                readonly property bool current: index === host.currentIndex
                width: Math.min(200, tabText.implicitWidth + 58)
                height: 32
                radius: 8
                color: current ? UI.Style.surfaceSelected : tabMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.title

                Rectangle {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 3.5
                    color: tabItem.modelData.ended ? UI.Style.inkDisabled : tabItem.modelData.colour
                }
                UI.Label {
                    id: tabText
                    x: 26
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, 130)
                    text: tabItem.modelData.title
                    size: 14
                    font.weight: tabItem.current ? UI.Style.weightSemiBold : UI.Style.weightMedium
                    color: tabItem.current ? UI.Style.ink : UI.Style.inkMuted
                }
                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                    onClicked: m => {
                        if (m.button === Qt.MiddleButton) {
                            host.closeTab(tabItem.index);
                        } else if (m.button === Qt.RightButton) {
                            root.openTabMenu(tabItem.index, tabItem, m.x, m.y);
                        } else {
                            host.currentIndex = tabItem.index;
                        }
                    }
                }
                UI.IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: 22
                    radius: 5
                    iconName: "close"
                    iconSize: 11
                    iconColor: UI.Style.iconMuted
                    tip: qsTr("Close tab")
                    focusPolicy: Qt.NoFocus
                    onClicked: host.closeTab(tabItem.index)
                }
            }
        }
        UI.IconButton {
            id: plusButton
            width: 32
            height: 32
            iconName: "plus"
            iconSize: 15
            iconColor: UI.Style.iconMuted
            tip: qsTr("New tab")
            focusPolicy: Qt.NoFocus
            onClicked: root.openPlusMenu()
        }
    }

    Row {
        id: tools
        anchors.right: windowButtons.left
        anchors.rightMargin: 14
        y: 6
        spacing: 2
        ToolButton {
            glyph: RemoteIcons.columns
            tip: root.tab && root.tab.split ? qsTr("One terminal") : qsTr("Two terminals side by side")
            on: root.tab !== null && root.tab.split
            enabled: root.tab !== null && !root.tab.filesOpen && !root.tab.ended
            onClicked: root.tab.toggleSplit()
        }
        ToolButton {
            glyph: RemoteIcons.folder
            tip: root.tab && root.tab.filesOpen ? qsTr("Back to the terminal") : qsTr("Files")
            on: root.tab !== null && root.tab.filesOpen
            enabled: root.tab !== null && !root.tab.local
            onClicked: root.tab.setFilesOpen(!root.tab.filesOpen)
        }
        ToolButton {
            glyph: RemoteIcons.snippets
            tip: host.snippetsOpen ? qsTr("Hide snippets") : qsTr("Snippets")
            on: host.snippetsOpen
            onClicked: host.snippetsOpen = !host.snippetsOpen
        }
        ToolButton {
            id: forwardButton
            glyph: RemoteIcons.forward
            tip: qsTr("Port forwarding")
            enabled: root.tab !== null && !root.tab.local
            onClicked: root.openForwardMenu()
        }
    }

    UI.WindowButtons {
        id: windowButtons
        anchors.right: parent.right
        handleActions: false
        buttonHeight: 46
        maximised: host.maximised
        windowActive: host.windowActive
        onMinimiseClicked: host.minimise()
        onMaximiseClicked: host.toggleMaximised()
        onCloseClicked: host.closeWindow()
    }

    // The snippets panel.
    Rectangle {
        id: panel
        visible: host.snippetsOpen
        x: parent.width - 332
        y: 46
        width: 332
        height: parent.height - 46 - 32
        color: root.barColour
        Rectangle {
            width: 1
            height: parent.height
            color: UI.Style.tone(0.06)
        }
        SnippetsPanel {
            anchors.fill: parent
            anchors.leftMargin: 17
            anchors.rightMargin: 16
            anchors.topMargin: 14
            anchors.bottomMargin: 16
            tab: root.tab
            runAll: root.runAll
            onRunAllToggled: root.runAll = !root.runAll
            onRunRequested: command => root.runSnippet(command)
        }
    }

    function runSnippet(command) {
        if (!root.tab) {
            return;
        }
        if (root.runAll && host.terminalTabCount() > 1) {
            runAllDialog.command = command;
            runAllDialog.open();
            return;
        }
        root.tab.run(command);
    }

    // The files, over where the terminal is.
    FilesView {
        visible: root.tab !== null && root.tab.filesOpen
        x: root.area.x
        y: root.area.y
        width: root.area.width
        height: root.area.height
        tab: root.tab
        onNotice: text => toast.show(text)
    }

    // The connection ended.
    Rectangle {
        visible: root.tab !== null && root.tab.ended && !root.tab.filesOpen
        x: root.area.x + 16
        y: root.area.y + root.area.height - height - 16
        width: root.area.width - 32
        height: 56
        radius: 12
        color: UI.Style.glassMenu
        border.width: 1
        border.color: UI.Style.borderMenu
        UI.Icon {
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            name: "info"
            size: 17
            color: UI.Style.iconInk
        }
        UI.Label {
            x: 46
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 46 - endButtons.width - 30
            text: root.tab && root.tab.local ? qsTr("The shell has ended.")
                : root.tab ? qsTr("The connection to %1 has ended. What ssh said is above.").arg(root.tab.title) : ""
            size: 14
        }
        Row {
            id: endButtons
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            UI.Button {
                size: "small"
                text: qsTr("Close tab")
                onClicked: host.closeTab(host.currentIndex)
            }
            UI.Button {
                size: "small"
                kind: "primary"
                text: root.tab && root.tab.local ? qsTr("Start again") : qsTr("Reconnect")
                onClicked: root.tab.reconnect()
            }
        }
    }

    // Suggestions under the cursor (RemoteTerminal.dc).
    Rectangle {
        id: suggestions
        visible: root.suggesting
        readonly property rect cursor: root.tab ? root.tab.cursor : Qt.rect(0, 0, 0, 0)
        width: 482
        height: suggestionColumn.implicitHeight + 12
        x: Math.min(root.area.x + Math.max(0, cursor.x - 28), root.area.x + root.area.width - width - 8)
        y: {
            const below = root.area.y + cursor.y + cursor.height + 10;
            return below + height > root.area.y + root.area.height ? root.area.y + cursor.y - height - 8 : below;
        }
        radius: 10
        color: UI.Style.glassMenu
        border.width: 1
        border.color: UI.Style.borderMenu

        Column {
            id: suggestionColumn
            x: 6
            y: 6
            width: parent.width - 12
            Repeater {
                model: root.suggesting ? root.tab.suggestions : []
                Rectangle {
                    id: suggestion
                    required property var modelData
                    required property int index
                    readonly property bool current: root.tab && index === root.tab.selected
                    width: suggestionColumn.width
                    height: 34
                    radius: 7
                    color: current ? UI.Style.surfaceSelected : "transparent"
                    UI.Glyph {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        path: suggestion.modelData.kind === "snippet" ? RemoteIcons.snippets : RemoteIcons.clock
                        color: suggestion.modelData.kind === "snippet" ? UI.Style.iconInk : UI.Style.iconMuted
                    }
                    UI.Label {
                        x: 34
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 34 - detail.width - 20
                        text: suggestion.modelData.text
                        size: 13
                        font.family: "JetBrains Mono"
                    }
                    UI.Label {
                        id: detail
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: suggestion.modelData.detail
                        size: 12
                        font.family: "JetBrains Mono"
                        color: UI.Style.inkSubtle
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.tab.accept(suggestion.index)
                    }
                }
            }
            UI.Label {
                leftPadding: 10
                topPadding: 6
                bottomPadding: 2
                text: qsTr("Tab to accept, keep typing to ignore. History stays on this PC")
                size: 12
                font.family: "JetBrains Mono"
                color: UI.Style.inkSubtle
            }
        }
    }

    // Status bar.
    RowLayout {
        x: 14
        y: parent.height - 32
        width: parent.width - 28
        height: 32
        spacing: 22
        visible: root.tab !== null

        UI.Label {
            text: root.tab ? root.tab.userHost : ""
            size: 13
            font.weight: UI.Style.weightSemiBold
        }
        Row {
            spacing: 7
            UI.Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "lock"
                size: 13
                color: UI.Style.success
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.tab ? root.tab.auth : ""
                size: 13
                color: UI.Style.inkMuted
            }
        }
        Row {
            visible: root.tab !== null && root.tab.forwards.length > 0
            spacing: 7
            UI.Glyph {
                anchors.verticalCenter: parent.verticalCenter
                width: 13
                height: 13
                path: RemoteIcons.forward
                color: UI.Style.iconMuted
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.tab && root.tab.forwards.length > 0 ? root.tab.forwards[0] + (root.tab.forwards.length > 1 ? qsTr(" and %1 more").arg(root.tab.forwards.length - 1) : "") : ""
                size: 13
                color: UI.Style.inkMuted
            }
        }
        Row {
            visible: root.tab !== null && root.tab.latency.length > 0
            spacing: 7
            UI.Glyph {
                anchors.verticalCenter: parent.verticalCenter
                width: 13
                height: 13
                path: RemoteIcons.signal
                color: UI.Style.success
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.tab ? root.tab.latency : ""
                size: 13
                color: UI.Style.inkMuted
                MouseArea {
                    id: latencyMouse
                    anchors.fill: parent
                    anchors.leftMargin: -20
                    hoverEnabled: true
                }
                UI.Tooltip {
                    text: qsTr("How long the server took to answer when this tab connected")
                    visible: latencyMouse.containsMouse
                }
            }
        }
        Item { Layout.fillWidth: true }
        UI.Label {
            text: "UTF-8"
            size: 13
            color: UI.Style.inkMuted
        }
        UI.Label {
            text: root.tab ? root.tab.size : ""
            size: 13
            color: UI.Style.inkMuted
        }
    }

    // Resize grips, outside the terminal.
    ResizeGrips {
        anchors.fill: parent
        enabled: !host.maximised
    }

    // The window's hairline edge, over everything.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: host.maximised ? 0 : 1
        border.color: UI.Style.windowEdge
        z: 900
    }

    function openTabMenu(index, item, x, y) {
        const t = host.tabs[index];
        tabMenu.entries = [
            { text: qsTr("Reconnect"), icon: "refresh", enabled: t.ended, run: () => t.reconnect() },
            { text: qsTr("Edit host"), icon: "pencil", enabled: !t.local, run: () => host.editHost(t.hostId) },
            { separator: true },
            { text: qsTr("Close tab"), icon: "close", shortcut: "Ctrl+Shift+W", run: () => host.closeTab(index) }
        ];
        tabMenu.popup(item, x, y);
    }
    function openPlusMenu() {
        const entries = [{ text: qsTr("This PC"), icon: "computer", shortcut: "Ctrl+Shift+T", run: () => host.openLocal() }, { separator: true }];
        for (const h of Hosts.hosts) {
            if (h.kind === "terminal") {
                entries.push({ text: h.name, icon: "terminal", run: () => host.openHost(h.id) });
            }
        }
        entries.push({ separator: true }, { text: qsTr("All hosts"), icon: "monitor", run: () => host.showHome() });
        plusMenu.entries = entries;
        plusMenu.popup(plusButton, 0, plusButton.height + 4);
    }
    function openForwardMenu() {
        const entries = [];
        for (const f of root.tab.forwards) {
            entries.push({ text: f, enabled: false });
        }
        if (entries.length === 0) {
            entries.push({ text: qsTr("No ports forwarded"), enabled: false });
        }
        entries.push({ separator: true }, { text: qsTr("Change forwards"), icon: "pencil", run: () => host.editHost(root.tab.hostId) });
        forwardMenu.entries = entries;
        forwardMenu.popup(forwardButton, 0, forwardButton.height + 4);
    }

    UI.Menu {
        id: tabMenu
    }
    // Right click in the terminal: a link's Open and Copy, then Copy, Paste
    // and Clear, as in Terminal.
    UI.Menu {
        id: terminalMenu
        minimumWidth: 200
    }
    Connections {
        target: root.tab
        ignoreUnknownSignals: true
        function onContextMenuRequested(x, y, canCopy, hasLink) {
            const tab = root.tab;
            const entries = [];
            if (hasLink) {
                entries.push({ text: qsTr("Open link"), icon: "external", run: () => tab.openLink() });
                entries.push({ text: qsTr("Copy link"), icon: "link", run: () => tab.copyLink() });
                entries.push({ separator: true });
            }
            entries.push({ text: qsTr("Copy"), icon: "menu-copy", shortcut: "Ctrl+Shift+C", enabled: canCopy, run: () => tab.copy() });
            entries.push({ text: qsTr("Paste"), icon: "menu-paste", shortcut: "Ctrl+V", run: () => tab.paste() });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Clear"), icon: "delete", run: () => tab.clear() });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Settings"), icon: "cog", shortcut: "Ctrl+,", run: () => host.showSettings() });
            terminalMenu.entries = entries;
            terminalMenu.popup(root, root.area.x + x, root.area.y + y);
        }
        // Terminal's "Check before pasting more than one line".
        function onPasteConfirmationNeeded(lines) {
            pasteDialog.forTab = root.tab;
            pasteDialog.lines = lines;
            pasteDialog.open();
        }
    }
    UI.Menu {
        id: plusMenu
        minimumWidth: 230
    }
    UI.Menu {
        id: forwardMenu
        minimumWidth: 260
    }

    UI.Dialog {
        id: runAllDialog
        property string command
        title: qsTr("Run in %1 tabs?").arg(host.terminalTabCount())
        text: qsTr("%1 runs on every open tab, which may be on different servers.").arg(command)
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: runAllDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Run in all")
                onClicked: {
                    host.runEverywhere(runAllDialog.command);
                    runAllDialog.close();
                }
            }
        ]
    }

    // As Terminal asks it: the main action on the right, and Enter takes it.
    UI.Dialog {
        id: pasteDialog
        property var forTab: null
        property int lines: 0
        property bool answered: false
        title: qsTr("Paste %1 lines?").arg(lines)
        text: qsTr("Each line may run as a command as soon as it is pasted. Check the text is what you meant to copy.")
        onAboutToShow: answered = false
        onClosed: {
            if (!answered && forTab) {
                forTab.answerPaste(false);
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
                    if (pasteDialog.forTab) {
                        pasteDialog.forTab.answerPaste(true);
                    }
                }
            }
        ]
    }

    SshSignInDialog {
        id: signInDialog
        onClosed: if (root.tab) root.tab.focusTerminal()
    }
    PassphraseDialog {
        id: questionDialog
        onClosed: if (root.tab) root.tab.focusTerminal()
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        z: 60
    }

    component ToolButton: UI.IconButton {
        property bool on: false
        width: 34
        height: 34
        radius: 8
        iconSize: 16
        iconColor: on ? UI.Style.accent : UI.Style.iconMuted
        stroke: 1.8
        checked: on
        focusPolicy: Qt.NoFocus
    }

    component ResizeGrips: Item {
        id: grips
        readonly property int g: 5
        MouseArea { width: grips.width; height: grips.g; cursorShape: Qt.SizeVerCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.TopEdge) }
        MouseArea { y: grips.height - grips.g; width: grips.width; height: grips.g; cursorShape: Qt.SizeVerCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.BottomEdge) }
        MouseArea { width: grips.g; height: grips.height; cursorShape: Qt.SizeHorCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.LeftEdge) }
        MouseArea { x: grips.width - grips.g; width: grips.g; height: grips.height; cursorShape: Qt.SizeHorCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.RightEdge) }
        MouseArea { width: 10; height: 10; cursorShape: Qt.SizeFDiagCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.TopEdge | Qt.LeftEdge) }
        MouseArea { x: grips.width - 10; width: 10; height: 10; cursorShape: Qt.SizeBDiagCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.TopEdge | Qt.RightEdge) }
        MouseArea { y: grips.height - 10; width: 10; height: 10; cursorShape: Qt.SizeBDiagCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.BottomEdge | Qt.LeftEdge) }
        MouseArea { x: grips.width - 10; y: grips.height - 10; width: 10; height: 10; cursorShape: Qt.SizeFDiagCursor; enabled: grips.enabled; onPressed: host.startResize(Qt.BottomEdge | Qt.RightEdge) }
    }
}
