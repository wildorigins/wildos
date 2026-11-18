pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ksvg as KSvg
import org.kde.plasma.private.sessions as Sessions
import WildOS.UI as UI

// The Start popup. The Plasma theme draws the glass, so this item stays
// transparent and only paints what sits on the glass. Sizes are taken from
// the design's outer edge, then the theme's frame margins are subtracted.
FocusScope {
    id: menu

    required property var launcher

    property string view: "home"
    readonly property bool searching: launcher.query.length > 0

    KSvg.FrameSvgItem {
        id: frame
        imagePath: "dialogs/background"
        visible: false
    }

    readonly property real edgeLeft: frame.margins.left
    readonly property real edgeRight: frame.margins.right
    readonly property real edgeTop: frame.margins.top
    readonly property real edgeBottom: frame.margins.bottom

    // The design measures padding inside a 1px glass border.
    readonly property real border: 1
    readonly property real homeGlassHeight: 2 * border + Style.padTop + 46 + Style.sectionGap
        + Math.max(home.restHeight, home.implicitHeight) + Style.sectionGap + Style.footerHeight
    // One height for the start page, All apps and search, as Windows 11's
    // Start keeps, so the window never jumps as you move between them: the
    // start page's own height, and never less than the search page needs.
    readonly property real glassHeight: Math.max(homeGlassHeight, Style.searchHeight)

    readonly property real contentWidth: Style.menuWidth - edgeLeft - edgeRight
    readonly property real contentHeight: glassHeight - edgeTop - edgeBottom

    Layout.minimumWidth: contentWidth
    Layout.preferredWidth: contentWidth
    Layout.maximumWidth: contentWidth
    Layout.minimumHeight: contentHeight
    Layout.preferredHeight: contentHeight
    Layout.maximumHeight: contentHeight

    focus: true

    function reset(): void {
        powerMenu.close();
        contextMenu.close();
        launcher.query = "";
        searchBox.clear();
        searchView.reset();
        view = "home";
        Nav.keyboard = false;
        home.pinnedGrid.currentIndex = -1;
        home.recentGrid.currentIndex = -1;
        home.showAllRecents = false;
        allApps.picking = false;
        allApps.list.currentIndex = -1;
        allApps.list.positionViewAtBeginning();
        searchBox.input.forceActiveFocus();
    }

    // Without this Plasma centres the popup on the Start button, which is left
    // of centre in the taskbar group (the anchor is the button itself only
    // with the taskbar aligned left). If the window type ever lacks a visual
    // parent the popup simply stays centred on the button.
    function centrePopup(): void {
        const popupWindow = menu.Window.window;
        const anchor = launcher.popupAnchor;
        if (!popupWindow || !anchor || !("visualParent" in popupWindow)) {
            return;
        }
        anchor.recentre();
        if (popupWindow.visualParent !== anchor) {
            popupWindow.visualParent = anchor;
        }
    }

    function showAllApps(): void {
        allApps.picking = false;
        view = "apps";
        allApps.list.positionViewAtBeginning();
        if (Nav.keyboard) {
            allApps.enter(false);
        }
    }

    // A hot corner or gesture opened Start for its app list.
    function openRequestedView(): void {
        if (launcher.openAtAllApps) {
            launcher.openAtAllApps = false;
            showAllApps();
        }
    }

    function showHome(): void {
        view = "home";
        if (Nav.keyboard) {
            home.allAppsButton.forceActiveFocus(Qt.BacktabFocusReason);
        }
    }

    function tabStops(): var {
        const footerStops = [footer.userButton, footer.filesButton, footer.settingsButton, footer.powerButton];
        if (searching) {
            const stops = [searchBox, searchView.chips];
            if (searchView.rows.length > 0) {
                stops.push(searchView.resultList, searchView.openButton);
                for (const button of searchView.actionButtons) {
                    if (button) {
                        stops.push(button);
                    }
                }
            }
            return stops;
        }
        if (view === "apps") {
            return [searchBox, allApps.backButton, allApps.list].concat(footerStops);
        }
        const stops = [searchBox];
        if (!home.showAllRecents) {
            stops.push(home.allAppsButton);
            if (home.pinnedGrid.count > 0) {
                stops.push(home.pinnedGrid);
            }
        }
        if (home.hasRecents) {
            if (home.moreButton.visible) {
                stops.push(home.moreButton);
            }
            stops.push(home.recentGrid);
        }
        return stops.concat(footerStops);
    }

    function cycleFocus(step: int): void {
        const stops = tabStops();
        let index = stops.findIndex(item => item.activeFocus);
        index = index < 0 ? 0 : (index + step + stops.length) % stops.length;
        const target = stops[index];
        if (target === home.pinnedGrid || target === home.recentGrid) {
            target.enter(false);
        } else if (target === allApps.list) {
            allApps.enter(false);
        } else {
            target.forceActiveFocus(step > 0 ? Qt.TabFocusReason : Qt.BacktabFocusReason);
        }
    }

    function focusSearch(): void {
        searchBox.input.forceActiveFocus(Qt.BacktabFocusReason);
    }

    // Context menus follow Windows: Open first, then the app or file commands
    // in Windows wording.
    function appEntries(item: Item, model: var, index: int): var {
        const data = "app" in item ? item.app : item.model;
        const entries = [{ text: qsTr("Open"), icon: "external", bold: true, run: () => {
            model.trigger(index, "", null);
            launcher.close();
        } }];
        const pinnedIndex = model === launcher.rootModel.favoritesModel ? index : -1;
        const commands = launcher.appCommands(String(data.favoriteId ?? ""), data.actionList,
            (actionId, argument) => model.trigger(index, actionId, argument), pinnedIndex);
        return commands.length > 0 ? entries.concat({ separator: true }, commands) : entries;
    }

    // Forgetting a file takes it out of the history, but the list is only
    // told when the file's type can still be looked up, and by then it has
    // been deleted with the rest (fixed in Plasma 6.6). So the list is read
    // again, once the history service has had time to save, and once more
    // for a slow disk.
    Timer {
        id: forgotten
        property int rounds: 0
        interval: 300
        onTriggered: {
            launcher.recentModel.refresh();
            rounds += 1;
            if (rounds < 2) {
                interval = 1200;
                start();
            }
        }
        function begin(): void {
            rounds = 0;
            interval = 300;
            restart();
        }
    }

    function documentEntries(item: Item, model: var, index: int): var {
        const actions = item.model.actionList ? Array.from(item.model.actionList) : [];
        const entries = [{ text: qsTr("Open"), icon: "external", bold: true, run: () => {
            model.trigger(index, "", null);
            launcher.close();
        } }];
        const folder = actions.find(a => a.actionId === "openParentFolder");
        if (folder) {
            entries.push({ text: qsTr("Open file location"), icon: "open-folder", run: () => {
                model.trigger(index, folder.actionId, folder.actionArgument);
                launcher.close();
            } });
        }
        const forget = actions.find(a => a.actionId === "forget");
        if (forget) {
            entries.push({ text: qsTr("Remove from list"), icon: "delete", run: () => {
                model.trigger(index, forget.actionId, forget.actionArgument);
                forgotten.begin();
            } });
        }
        return entries;
    }

    function showItemMenu(model: var, index: int, item: Item, x: real, y: real, kind: string): void {
        showMenu(kind === "document" ? documentEntries(item, model, index) : appEntries(item, model, index), item, x, y);
    }

    function showMenu(entries: var, item: Item, x: real, y: real): void {
        if (entries.length === 0) {
            return;
        }
        // Right-click menus are the 280 px, 34 px row menu of MenusShell.dc;
        // the account menu shares the window with PowerMenu.dc's 40 px rows.
        contextMenu.menuWidth = 280;
        contextMenu.rowHeight = UI.Style.menuItemHeight;
        contextMenu.entries = entries;
        contextMenu.openAt(item, x, y);
    }

    // Like the account button on Windows 11 Start: a small menu rather than a
    // jump straight into Settings.
    function toggleAccountMenu(): void {
        if (contextMenu.opened) {
            contextMenu.close();
            return;
        }
        const session = launcher.session;
        contextMenu.menuWidth = 250;
        contextMenu.rowHeight = 40;
        contextMenu.entries = [
            { text: qsTr("Change account settings"), icon: "cog", run: () => launcher.openAccountSettings() },
            { separator: true },
            { text: qsTr("Lock"), icon: "lock", run: () => {
                launcher.close();
                session.lock();
            } },
            { text: qsTr("Sign out"), icon: "sign-out", run: () => session.requestLogout(Sessions.SessionManagement.ConfirmationMode.Skip) },
        ];
        contextMenu.openAboveAt(footer.userButton, 0);
    }

    function togglePowerMenu(): void {
        if (powerMenu.opened) {
            powerMenu.close();
            return;
        }
        const session = launcher.session;
        const entries = [
            { text: qsTr("Lock"), icon: "lock", run: () => {
                launcher.close();
                session.lock();
            } },
            { text: qsTr("Sign out"), icon: "sign-out", run: () => session.requestLogout(Sessions.SessionManagement.ConfirmationMode.Skip) },
            { separator: true },
        ];
        if (session.canSuspend) {
            entries.push({ text: qsTr("Sleep"), icon: "moon", run: () => {
                launcher.close();
                session.suspend();
            } });
        }
        entries.push(
            { text: qsTr("Restart"), icon: "refresh", run: () => session.requestReboot(Sessions.SessionManagement.ConfirmationMode.Skip) },
            { text: qsTr("Shut down"), icon: "power", run: () => session.requestShutdown(Sessions.SessionManagement.ConfirmationMode.Skip) },
        );
        powerMenu.entries = entries;
        powerMenu.openAbove(footer.powerButton, 12);
    }

    Connections {
        target: menu.launcher
        function onExpandedChanged() {
            if (menu.launcher.expanded) {
                menu.centrePopup();
                menu.reset();
                menu.openRequestedView();
            } else {
                powerMenu.close();
                contextMenu.close();
            }
        }
    }

    // Any typing that is not already going into the search box is sent there,
    // so people can start typing the moment Start opens.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            if (searching) {
                searchBox.clear();
                searchBox.input.forceActiveFocus();
            } else if (view === "apps" && allApps.picking) {
                allApps.picking = false;
            } else if (view === "apps") {
                showHome();
                searchBox.input.forceActiveFocus();
            } else {
                launcher.close();
            }
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            Nav.keyboard = true;
            cycleFocus(event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? -1 : 1);
            event.accepted = true;
            return;
        }
        if (searchBox.input.activeFocus) {
            return;
        }
        if (event.key === Qt.Key_Backspace && searching) {
            searchBox.input.forceActiveFocus();
            searchBox.input.remove(searchBox.input.length - 1, searchBox.input.length);
            event.accepted = true;
            return;
        }
        const blocked = Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier;
        if (event.text.length > 0 && !(event.modifiers & blocked) && event.text.charCodeAt(0) >= 32 && event.text.charCodeAt(0) !== 127) {
            searchBox.type(event.text);
            event.accepted = true;
        }
    }

    // Qt re-sends hover events whenever the scene changes, even with the
    // pointer still, so only real movement hides the focus rings.
    HoverHandler {
        property point last: Qt.point(-1, -1)
        onPointChanged: {
            const p = point.scenePosition;
            if (Math.abs(p.x - last.x) + Math.abs(p.y - last.y) > 2) {
                last = p;
                Nav.keyboard = false;
            }
        }
    }

    Item {
        id: content
        anchors.fill: parent
        anchors.leftMargin: menu.border + Style.padSide - menu.edgeLeft
        anchors.rightMargin: menu.border + Style.padSide - menu.edgeRight
        anchors.topMargin: menu.border + Style.padTop - menu.edgeTop
        anchors.bottomMargin: menu.border + (menu.searching ? 46 : Style.footerHeight) - menu.edgeBottom

        SearchBox {
            id: searchBox
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 46
            focus: true
            onTextChanged: menu.launcher.query = text
            onAccepted: {
                if (menu.searching) {
                    searchView.open(searchView.current);
                }
            }
            onStepSelection: step => searchView.moveSelection(step)
            onMoveDown: {
                if (menu.view === "apps") {
                    allApps.enter(false);
                } else if (home.pinnedGrid.count > 0 && !home.showAllRecents) {
                    home.pinnedGrid.enter(false);
                } else if (home.hasRecents) {
                    home.recentGrid.enter(false);
                } else {
                    footer.userButton.forceActiveFocus(Qt.TabFocusReason);
                }
            }
            onMoveUp: footer.userButton.forceActiveFocus(Qt.BacktabFocusReason)
            onEscapePressed: {
                if (menu.view === "apps" && allApps.picking) {
                    allApps.picking = false;
                } else if (menu.view === "apps") {
                    menu.showHome();
                    searchBox.input.forceActiveFocus();
                } else {
                    menu.launcher.close();
                }
            }
        }

        HomeView {
            id: home
            anchors.top: searchBox.bottom
            anchors.topMargin: Style.sectionGap
            anchors.left: parent.left
            anchors.right: parent.right
            height: implicitHeight
            visible: !menu.searching && menu.view === "home"
            // Coming back to the start page it slides in from the left, the
            // way All apps slid in from the right.
            transform: UI.Reveal {
                item: home
                dx: -UI.Style.travelMedium
            }
            launcher: menu.launcher
            onAllAppsRequested: menu.showAllApps()
            onMoreRequested: home.showAllRecents = !home.showAllRecents
            onLeaveUp: menu.focusSearch()
            onLeaveDown: footer.userButton.forceActiveFocus(Qt.TabFocusReason)
            onMenuRequested: (model, index, item, x, y, pinned) => menu.showItemMenu(model, index, item, x, y, pinned ? "pinned" : "document")
        }

        AllAppsView {
            id: allApps
            anchors.top: searchBox.bottom
            anchors.topMargin: Style.sectionGap
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Style.sectionGap
            visible: !menu.searching && menu.view === "apps"
            transform: UI.Reveal {
                item: allApps
                dx: UI.Style.travelMedium
            }
            launcher: menu.launcher
            onBackRequested: menu.showHome()
            onLeaveUp: menu.focusSearch()
            onLeaveDown: footer.userButton.forceActiveFocus(Qt.TabFocusReason)
            onMenuRequested: (model, index, item, x, y) => menu.showItemMenu(model, index, item, x, y, "app")
        }

        SearchView {
            id: searchView
            anchors.top: searchBox.bottom
            anchors.topMargin: 18
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 18
            visible: menu.searching
            // Results rise into place as the first letters go in.
            transform: UI.Reveal {
                item: searchView
                dy: UI.Style.travelSmall
            }
            launcher: menu.launcher
            onLeaveUp: menu.focusSearch()
            onMenuRequested: (entries, item, x, y) => menu.showMenu(entries, item, x, y)
        }
    }


    // The footer and the privacy strip run edge to edge across the glass, so
    // they reach out over the theme's frame margins.
    Footer {
        id: footer
        anchors.left: parent.left
        anchors.leftMargin: menu.border - menu.edgeLeft
        anchors.right: parent.right
        anchors.rightMargin: menu.border - menu.edgeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: menu.border - menu.edgeBottom
        height: Style.footerHeight
        visible: !menu.searching
        launcher: menu.launcher
        powerMenuOpen: powerMenu.opened
        accountMenuOpen: contextMenu.opened && contextMenu.returnFocus === footer.userButton
        onPowerRequested: menu.togglePowerMenu()
        onAccountRequested: menu.toggleAccountMenu()
        onLeaveUp: {
            if (menu.view === "apps") {
                allApps.enter(true);
            } else if (home.hasRecents) {
                home.recentGrid.enter(true);
            } else if (home.pinnedGrid.count > 0 && !home.showAllRecents) {
                home.pinnedGrid.enter(true);
            } else {
                menu.focusSearch();
            }
        }

        BottomBand {
            anchors.fill: parent
            z: -1
        }
    }

    BottomBand {
        id: privacyStrip
        anchors.left: parent.left
        anchors.leftMargin: menu.border - menu.edgeLeft
        anchors.right: parent.right
        anchors.rightMargin: menu.border - menu.edgeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: menu.border - menu.edgeBottom
        height: 46
        visible: menu.searching

        Row {
            x: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            UI.Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "lock"
                size: 15
                strokeWidth: 2
                color: UI.Style.accent
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                size: 12
                color: UI.Style.inkStatus
                text: qsTr("Search stays on this computer. Nothing you type is sent online.")
            }
        }
    }

    PopupMenu {
        id: powerMenu
    }

    PopupMenu {
        id: contextMenu
    }

    Window.onWindowChanged: centrePopup()
    onVisibleChanged: {
        if (visible) {
            centrePopup();
        }
    }

    Component.onCompleted: {
        centrePopup();
        reset();
        openRequestedView();
    }
}
