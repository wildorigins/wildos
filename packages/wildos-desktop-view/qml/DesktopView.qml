pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.UI as UI

// The Wild OS desktop (DesktopIcons.dc): icons in a Windows-style column
// grid down the left, the desktop's right-click menu, and everything the
// icons do (open, drag and drop, rename, delete to the Recycle Bin, undo the
// last of those with Ctrl+Z, select with a rubber band, keyboard
// navigation, Ctrl and the wheel for the icon size). The containment hosts this
// over Plasma's wallpaper; `config` is its Plasmoid.configuration.
FocusScope {
    id: view

    property var config: null
    // Icons live on the main screen only, as on Windows. The other screens
    // get the wallpaper and the menu.
    property bool primary: true
    // The part of the screen the taskbar leaves free, in this item's
    // coordinates.
    property rect area: Qt.rect(0, 0, width, height)
    // What the menus blur behind their glass: the containment, wallpaper
    // and icons included.
    property Item backdrop: null
    // Plasma widgets on the desktop, allowed in Settings > Personalisation.
    property bool widgetsAllowed: false
    // Given by the containment: the widget (its Plasma applet) under a point
    // of this view, or null, so a right-click on a widget can offer it.
    property var widgetAt: null
    property var menuWidget: null
    signal addWidgetsRequested()

    readonly property alias model: desktopModel
    readonly property bool iconsShown: primary && conf("showIcons", true)
    readonly property bool binFull: DeskOps.binCount > 0

    // Medium is the design's 48px icon in a 92px tile on a 100px grid.
    // Large and small follow Windows' 96 and 32.
    readonly property int sizeChoice: conf("iconSize", 1)
    readonly property int iconSize: sizeChoice === 2 ? 96 : sizeChoice === 0 ? 32 : 48
    readonly property real itemWidth: sizeChoice === 2 ? 120 : sizeChoice === 0 ? 76 : 92
    readonly property real lineHeight: UI.Style.px(12) * 1.3
    readonly property real itemHeight: 9 + iconSize + 6 + lineHeight + 7
    readonly property real cellWidth: itemWidth + 8
    readonly property real cellHeight: Math.max(Math.ceil(itemHeight + 14), iconSize + 52)

    property int renameRow: -1
    property bool keyboardUsed: false
    property bool dragging: false
    property int dragAnchorRow: -1
    property point dragStart
    readonly property string dragToken: "wildos-desktop-" + Date.now() + "-" + Math.floor(Math.random() * 1e9)
    property string typed: ""

    function conf(key, fallback) {
        if (!config || config[key] === undefined || config[key] === null) {
            return fallback;
        }
        return config[key];
    }

    function setConf(key, value) {
        if (config && config[key] !== value) {
            config[key] = value;
        }
    }

    function pointerUsed() {
        keyboardUsed = false;
    }

    // A press anywhere on the desktop takes the keyboard back from whatever
    // inside it holds it, so an open name editor loses focus and keeps the
    // new name, as clicking away does on Windows. forceActiveFocus() alone
    // is not enough: this item is a FocusScope, so it hands active focus
    // straight back to the child that had it.
    function takeFocus() {
        const w = view.Window.window;
        const item = w ? w.activeFocusItem : null;
        if (item && item !== view) {
            item.focus = false;
        }
        view.forceActiveFocus();
    }

    // Commands shared by the menus and the keyboard.

    function selectedRowsOfKind(kind) {
        return desktopModel.selectedRows().filter(r => desktopModel.kindAt(r) === kind);
    }

    function openSelection() {
        const files = [];
        const folders = [];
        for (const row of desktopModel.selectedRows()) {
            if (desktopModel.kindAt(row) === "item" && !desktopModel.isDirAt(row)) {
                files.push(desktopModel.urlAt(row));
            } else {
                folders.push(desktopModel.urlAt(row));
            }
        }
        DeskOps.open(files);
        DeskOps.openInFiles(folders);
    }

    function rename() {
        const rows = desktopModel.selectedRows();
        if (rows.length !== 1 || desktopModel.kindAt(rows[0]) !== "item") {
            return;
        }
        startRename(rows[0]);
    }

    function startRename(row) {
        if (!iconsShown) {
            return;
        }
        desktopModel.selectOnly(row);
        renameRow = row;
    }

    function finishRename(row, text) {
        renameRow = -1;
        if (desktopModel.kindAt(row) === "item") {
            DeskOps.rename(desktopModel.urlAt(row), desktopModel.fileNameAt(row), desktopModel.isDirAt(row), text);
        }
        view.forceActiveFocus();
    }

    function cancelRename() {
        renameRow = -1;
        view.forceActiveFocus();
    }

    function remove(forGood) {
        const urls = desktopModel.selectedUrls();
        if (urls.length === 0) {
            return;
        }
        if (forGood) {
            DeskOps.deleteForGood(urls);
        } else {
            DeskOps.trash(urls);
        }
    }

    function properties() {
        const rows = desktopModel.selectedRows();
        if (rows.length === 1 && desktopModel.kindAt(rows[0]) === "computer") {
            // Windows opens System > About from This PC's Properties.
            DeskOps.openSettings("about");
            return;
        }
        if (rows.length === 1 && desktopModel.kindAt(rows[0]) === "home") {
            propertiesDialog.show([desktopModel.urlAt(rows[0])]);
            return;
        }
        const urls = desktopModel.selectedUrls();
        if (urls.length > 0) {
            propertiesDialog.show(urls);
        }
    }

    function chooseApp(urls) {
        openWithDialog.show(urls);
    }

    function newFolder() {
        DeskOps.newFolder(desktopModel.folder);
    }

    function inGrid(x, y) {
        return Qt.point(x - icons.x, y - icons.y);
    }

    // Drag and drop

    function startDrag(tile, pressPoint) {
        if (dragging || renameRow >= 0) {
            return;
        }
        const urls = desktopModel.selectedUrls();
        dragAnchorRow = tile.index;
        dragStart = pressPoint;
        dragging = true;
        const mime = { "application/x-wildos-desktop": dragToken };
        if (urls.length > 0) {
            mime["text/uri-list"] = urls.map(u => u.toString()).join("\r\n");
        }
        dragProxy.Drag.mimeData = mime;
        dragProxy.Drag.supportedActions = urls.length > 0 ? (Qt.CopyAction | Qt.MoveAction | Qt.LinkAction) : Qt.MoveAction;
        dragProxy.Drag.proposedAction = Qt.MoveAction;
        const hot = tile.mapFromItem(view, pressPoint.x, pressPoint.y);
        tile.grabToImage(result => {
            dragProxy.Drag.imageSource = result.url;
            dragProxy.Drag.hotSpot = Qt.point(hot.x, hot.y);
            dragProxy.Drag.active = true;
        });
    }

    function isOurs(drop) {
        return drop.formats.indexOf("application/x-wildos-desktop") >= 0
            && drop.getDataAsString("application/x-wildos-desktop") === dragToken;
    }

    function dropOnDesktop(drop) {
        if (isOurs(drop)) {
            desktopModel.moveSelection(dragAnchorRow, drop.x - dragStart.x, drop.y - dragStart.y);
            drop.accept(Qt.MoveAction);
            return;
        }
        const urls = drop.urls;
        if (!urls || urls.length === 0) {
            return;
        }
        const p = inGrid(drop.x, drop.y);
        desktopModel.setPlaceHint(p.x, p.y);
        const action = DeskOps.dropAction(urls, desktopModel.folder, 0);
        drop.accept(action);
        DeskOps.drop(urls, desktopModel.folder, action);
    }

    function dropOnItem(row, drop) {
        const target = desktopModel.urlAt(row);
        const urls = isOurs(drop) ? desktopModel.selectedUrls().filter(u => u.toString() !== target.toString()) : drop.urls;
        if (!urls || urls.length === 0) {
            drop.accept(Qt.IgnoreAction);
            return;
        }
        if (desktopModel.kindAt(row) === "bin") {
            drop.accept(Qt.MoveAction);
            DeskOps.trash(urls);
            return;
        }
        const action = DeskOps.dropAction(urls, target, 0);
        drop.accept(action);
        DeskOps.drop(urls, target, action);
    }

    // Menus

    function viewEntries() {
        const shown = conf("showIcons", true);
        return [
            { text: qsTr("Large icons"), checked: sizeChoice === 2, run: () => view.setConf("iconSize", 2) },
            { text: qsTr("Medium icons"), checked: sizeChoice === 1, run: () => view.setConf("iconSize", 1) },
            { text: qsTr("Small icons"), checked: sizeChoice === 0, run: () => view.setConf("iconSize", 0) },
            { separator: true },
            { text: qsTr("Auto arrange"), checked: desktopModel.autoArrange, run: () => view.setConf("autoArrange", !desktopModel.autoArrange) },
            { text: qsTr("Align to grid"), checked: desktopModel.alignToGrid, run: () => view.setConf("alignToGrid", !desktopModel.alignToGrid) },
            { text: qsTr("Show icons"), checked: shown, run: () => view.setConf("showIcons", !shown) }
        ];
    }

    function sortEntries() {
        const key = desktopModel.sortKey;
        return [
            { text: qsTr("Name"), radio: true, checked: key === "name", run: () => desktopModel.sortBy("name") },
            { text: qsTr("Size"), radio: true, checked: key === "size", run: () => desktopModel.sortBy("size") },
            { text: qsTr("Item type"), radio: true, checked: key === "type", run: () => desktopModel.sortBy("type") },
            { text: qsTr("Date modified"), radio: true, checked: key === "modified", run: () => desktopModel.sortBy("modified") }
        ];
    }

    // The same New list as Files' (both come from the same newitems.cpp).
    function newEntries() {
        const folder = desktopModel.folder;
        const entries = [
            { text: qsTr("Folder"), icon: "new-folder", shortcut: qsTr("Ctrl+Shift+N"), run: () => DeskOps.newFolder(folder) },
            { text: qsTr("Shortcut"), icon: "link", run: () => shortcutDialog.show(folder) },
            { separator: true }
        ];
        for (const type of DeskOps.newFileTypes()) {
            const id = type.id;
            entries.push({ text: type.text, themeIcon: type.icon + ",text-x-generic", run: () => DeskOps.newFile(folder, id) });
        }
        return entries;
    }

    // MenusShell.dc "Desktop" (board 4.2's menu with the right-click menu
    // rules): View holds the icon options so the top level stays short.
    function desktopEntries() {
        const entries = [
            { text: qsTr("View"), icon: "desktop-view", submenu: viewEntries(), submenuWidth: 176 },
            { text: qsTr("Sort by"), icon: "menu-sort", submenu: sortEntries(), submenuWidth: 176 },
            { text: qsTr("Refresh"), icon: "menu-refresh", shortcut: "F5", run: () => desktopModel.refresh() },
            { separator: true },
            { text: qsTr("New"), icon: "plus", submenu: newEntries(), submenuWidth: 250 },
            { text: qsTr("Paste"), icon: "menu-paste", shortcut: "Ctrl+V", enabled: DeskOps.canPaste, run: () => DeskOps.paste(desktopModel.folder) },
            { separator: true },
            { text: qsTr("Display settings"), icon: "menu-desktop", run: () => DeskOps.openSettings("display") },
            { text: qsTr("Personalise"), icon: "menu-personalise", run: () => DeskOps.openSettings("personalisation") },
            { separator: true },
            { text: qsTr("Open in Terminal"), icon: "terminal", run: () => DeskOps.openTerminal(desktopModel.folder) }
        ];
        if (widgetsAllowed) {
            entries.splice(entries.length - 2, 0, { text: qsTr("Add widgets"), icon: "menu-apps", run: () => view.addWidgetsRequested() });
        }
        // A right-click on a widget leads with what can be done to it; press
        // and hold still moves and resizes it, as in Plasma.
        const widget = menuWidget;
        if (widget) {
            const configure = widget.internalAction("configure");
            const remove = widget.internalAction("remove");
            const top = [];
            if (configure) {
                top.push({ text: qsTr("Widget settings"), icon: "menu-settings", run: () => configure.trigger() });
            }
            if (remove) {
                top.push({ text: qsTr("Remove widget"), icon: "menu-uninstall", danger: true, run: () => remove.trigger() });
            }
            if (top.length > 0) {
                entries.unshift(...top, { separator: true });
            }
        }
        // Under Paste, and only while there is something to take back, as
        // on Windows.
        const kind = DeskOps.undoKind;
        if (kind.length > 0) {
            const text = kind === "delete" ? qsTr("Undo delete") : kind === "move" ? qsTr("Undo move") : qsTr("Undo rename");
            entries.splice(6, 0, { text: text, icon: "undo", shortcut: "Ctrl+Z", run: () => DeskOps.undo() });
        }
        return entries;
    }

    // The item menu is Files' (FilesView.qml openItemMenu) for the desktop:
    // the same entries in the same order, without tabs.
    function itemEntries() {
        const m = desktopModel;
        const rows = m.selectedRows();
        const urls = m.selectedUrls();
        const one = rows.length === 1;
        const row = one ? rows[0] : -1;
        const kind = one ? m.kindAt(row) : "item";
        const entries = [];

        if (kind === "computer") {
            entries.push({ text: qsTr("Open"), icon: "open", bold: true, shortcut: qsTr("Enter"), run: () => view.openSelection() });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Properties"), icon: "info", shortcut: qsTr("Alt+Enter"), run: () => DeskOps.openSettings("about") });
            return entries;
        }
        // MenusShell.dc "Bin". Emptying always asks first (DeskOps.emptyBin).
        // The board's Properties row is left out: the Recycle Bin has no
        // settings of its own to show yet.
        if (kind === "bin") {
            entries.push({ text: qsTr("Open"), icon: "open", bold: true, shortcut: qsTr("Enter"), run: () => view.openSelection() });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Empty Recycle Bin"), icon: "menu-uninstall", danger: true, enabled: DeskOps.binCount > 0, run: () => DeskOps.emptyBin() });
            return entries;
        }
        if (kind === "home") {
            const home = m.urlAt(row);
            entries.push({ text: qsTr("Open"), icon: "open", bold: true, shortcut: qsTr("Enter"), run: () => view.openSelection() });
            entries.push({ text: qsTr("Paste into folder"), icon: "paste", enabled: DeskOps.canPaste, run: () => DeskOps.paste(home) });
            entries.push({ text: qsTr("Copy path"), icon: "link", run: () => DeskOps.copyPaths([home]) });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Open in Terminal"), icon: "terminal", run: () => DeskOps.openTerminal(home) });
            entries.push({ text: qsTr("Properties"), icon: "info", shortcut: qsTr("Alt+Enter"), run: () => view.properties() });
            return entries;
        }

        const isDir = one && m.isDirAt(row);
        entries.push({ text: qsTr("Open"), icon: "open", bold: true, shortcut: qsTr("Enter"), run: () => view.openSelection() });
        if (one && !isDir) {
            const apps = DeskOps.appsFor(urls[0]).map(a => ({ text: a.name, themeIcon: a.icon + ",application-x-executable", run: () => DeskOps.openWith(urls, a.id, false) }));
            if (apps.length > 0) {
                apps.push({ separator: true });
            }
            apps.push({ text: qsTr("Choose another app"), icon: "open-with", run: () => view.chooseApp(urls) });
            entries.push({ text: qsTr("Open with"), icon: "open-with", submenu: apps });
            // An app shortcut or a program such as an AppImage, on a PC with
            // two graphics cards.
            const target = urls[0].toString();
            if (UI.System.twoGraphicsCards() && UI.System.canChooseGraphics(target)) {
                const dedicated = UI.System.dedicatedGraphics(target);
                entries.push({ text: qsTr("Dedicated graphics card"), checked: dedicated,
                    run: () => UI.System.setDedicatedGraphics(target, !dedicated) });
            }
        }
        entries.push({ separator: true });
        entries.push({ text: qsTr("Cut"), icon: "cut", shortcut: "Ctrl+X", enabled: urls.length > 0, run: () => DeskOps.cut(urls) });
        entries.push({ text: qsTr("Copy"), icon: "copy", shortcut: "Ctrl+C", enabled: urls.length > 0, run: () => DeskOps.copy(urls) });
        if (isDir) {
            entries.push({ text: qsTr("Paste into folder"), icon: "paste", enabled: DeskOps.canPaste, run: () => DeskOps.paste(urls[0]) });
        }
        entries.push({ text: qsTr("Copy path"), icon: "link", enabled: urls.length > 0, run: () => DeskOps.copyPaths(urls) });
        entries.push({ separator: true });
        entries.push({ text: qsTr("Rename"), icon: "rename", shortcut: "F2", enabled: one, run: () => view.rename() });
        entries.push({ text: qsTr("Delete"), icon: "delete", shortcut: qsTranslate("Keys", "Delete"), enabled: urls.length > 0, run: () => view.remove(false) });
        entries.push({ separator: true });
        if (isDir) {
            entries.push({ text: qsTr("Open in Terminal"), icon: "terminal", run: () => DeskOps.openTerminal(urls[0]) });
        }
        entries.push({ text: qsTr("Properties"), icon: "info", shortcut: qsTr("Alt+Enter"), enabled: urls.length > 0, run: () => view.properties() });
        return entries;
    }

    function showMenu(entries, x, y, width) {
        menu.close();
        const w = Math.round(width * Math.max(1, UI.Style.textScale));
        menu.minimumWidth = w;
        menu.maximumWidth = Math.max(380, w);
        menu.entries = entries;
        menu.popup(view, x, y);
        if (keyboardUsed) {
            for (let i = 0; i < entries.length; ++i) {
                if (!entries[i].separator && entries[i].enabled !== false) {
                    menu.currentIndex = i;
                    break;
                }
            }
        }
    }

    function openDesktopMenu(x, y) {
        desktopModel.clearSelection();
        const p = inGrid(x, y);
        desktopModel.setPlaceHint(p.x, p.y);
        menuWidget = widgetsAllowed && widgetAt ? widgetAt(x, y) : null;
        showMenu(desktopEntries(), x, y, 250);
    }

    function openItemMenu(x, y) {
        showMenu(itemEntries(), x, y, 252);
    }

    function menuFromKeyboard() {
        keyboardUsed = true;
        const m = desktopModel;
        if (m.selectedCount > 0 && iconsShown) {
            const r = m.itemRect(m.currentIndex >= 0 && m.isSelected(m.currentIndex) ? m.currentIndex : m.selectedRows()[0]);
            openItemMenu(icons.x + r.x + r.width / 2, icons.y + r.y + r.height / 2);
        } else {
            openDesktopMenu(area.x + area.width / 2, area.y + area.height / 3);
        }
    }

    function handleKey(event) {
        const m = desktopModel;
        const ctrl = event.modifiers & Qt.ControlModifier;
        const shift = event.modifiers & Qt.ShiftModifier;
        const alt = event.modifiers & Qt.AltModifier;
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Right:
        case Qt.Key_Up:
        case Qt.Key_Down:
        case Qt.Key_Home:
        case Qt.Key_End: {
            if (!iconsShown || m.count === 0) {
                return;
            }
            keyboardUsed = true;
            let next;
            if (event.key === Qt.Key_Home) {
                next = 0;
            } else if (event.key === Qt.Key_End) {
                next = m.count - 1;
            } else {
                next = m.currentIndex < 0 ? 0 : m.neighbour(m.currentIndex, event.key);
            }
            if (shift) {
                m.select(next, Qt.ShiftModifier);
            } else if (ctrl) {
                m.currentIndex = next;
            } else {
                m.selectOnly(next);
            }
            event.accepted = true;
            return;
        }
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (alt) {
                properties();
            } else {
                openSelection();
            }
            event.accepted = true;
            return;
        case Qt.Key_F2:
            rename();
            event.accepted = true;
            return;
        case Qt.Key_Delete:
            remove(shift);
            event.accepted = true;
            return;
        case Qt.Key_F5:
            m.refresh();
            event.accepted = true;
            return;
        case Qt.Key_Escape:
            m.clearSelection();
            event.accepted = true;
            return;
        case Qt.Key_Menu:
            menuFromKeyboard();
            event.accepted = true;
            return;
        case Qt.Key_F10:
            if (shift) {
                menuFromKeyboard();
                event.accepted = true;
            }
            return;
        case Qt.Key_Space:
            if (ctrl && m.currentIndex >= 0) {
                m.select(m.currentIndex, Qt.ControlModifier);
                event.accepted = true;
                return;
            }
            break;
        case Qt.Key_A:
            if (ctrl) {
                m.selectAll();
                event.accepted = true;
                return;
            }
            break;
        case Qt.Key_C:
            if (ctrl) {
                DeskOps.copy(m.selectedUrls());
                event.accepted = true;
                return;
            }
            break;
        case Qt.Key_X:
            if (ctrl) {
                DeskOps.cut(m.selectedUrls());
                event.accepted = true;
                return;
            }
            break;
        case Qt.Key_V:
            if (ctrl) {
                DeskOps.paste(m.folder);
                event.accepted = true;
                return;
            }
            break;
        case Qt.Key_N:
            if (ctrl && shift) {
                newFolder();
                event.accepted = true;
                return;
            }
            break;
        case Qt.Key_Z:
            if (ctrl && !shift) {
                DeskOps.undo();
                event.accepted = true;
                return;
            }
            break;
        }
        if (!ctrl && !alt && iconsShown && event.text.length === 1 && event.text.trim().length === 1) {
            keyboardUsed = true;
            typed += event.text;
            typeAhead.restart();
            let row = m.findByPrefix(typed, typed.length === 1 ? m.currentIndex : m.currentIndex - 1);
            if (row < 0 && typed.length > 1) {
                typed = event.text;
                row = m.findByPrefix(typed, m.currentIndex);
            }
            if (row >= 0) {
                m.selectOnly(row);
            }
            event.accepted = true;
        }
    }

    // Keys the desktop answers itself, claimed before Plasma's own actions
    // (it binds Delete among others) can take them as shortcuts.
    function ownsKey(event) {
        const ctrl = event.modifiers & Qt.ControlModifier;
        switch (event.key) {
        case Qt.Key_Delete:
        case Qt.Key_F2:
        case Qt.Key_F5:
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Escape:
        case Qt.Key_Menu:
        case Qt.Key_Left:
        case Qt.Key_Right:
        case Qt.Key_Up:
        case Qt.Key_Down:
        case Qt.Key_Home:
        case Qt.Key_End:
            return true;
        case Qt.Key_A:
        case Qt.Key_C:
        case Qt.Key_X:
        case Qt.Key_V:
        case Qt.Key_N:
        case Qt.Key_Z:
            return ctrl;
        }
        return false;
    }

    focus: true
    activeFocusOnTab: true
    Keys.onShortcutOverride: event => event.accepted = ownsKey(event)
    Keys.onPressed: event => handleKey(event)

    Timer {
        id: typeAhead
        interval: 1000
        onTriggered: view.typed = ""
    }

    DesktopModel {
        id: desktopModel
        cellWidth: view.cellWidth
        cellHeight: view.cellHeight
        itemWidth: view.itemWidth
        itemHeight: view.itemHeight
        areaWidth: icons.width
        areaHeight: icons.height
        sortKey: view.conf("sortKey", "name")
        sortDescending: view.conf("sortDescending", false)
        autoArrange: view.conf("autoArrange", false)
        alignToGrid: view.conf("alignToGrid", true)

        Component.onCompleted: positions = view.conf("positions", "")
        onPositionsChanged: view.setConf("positions", positions)
        onSortChanged: {
            view.setConf("sortKey", sortKey);
            view.setConf("sortDescending", sortDescending);
        }
        onRenameRequested: row => view.startRename(row)
    }

    Connections {
        target: view.config ? view.config : null
        ignoreUnknownSignals: true
        function onValueChanged(key, value) {
            if (key === "positions" && value !== desktopModel.positions) {
                desktopModel.positions = value;
            }
        }
    }

    MouseArea {
        id: backgroundArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: Qt.ArrowCursor

        property point start
        property bool banding: false
        property rect band
        // A touchpad sends many small steps; one notch of a wheel is 120.
        property real wheelTravel: 0

        onWheel: wheel => {
            if (!(wheel.modifiers & Qt.ControlModifier) || !view.iconsShown) {
                wheel.accepted = false;
                return;
            }
            const delta = wheel.angleDelta.y;
            wheelTravel = (delta > 0) === (wheelTravel > 0) ? wheelTravel + delta : delta;
            if (Math.abs(wheelTravel) >= 120) {
                view.setConf("iconSize", Math.max(0, Math.min(2, view.sizeChoice + (wheelTravel > 0 ? 1 : -1))));
                wheelTravel = 0;
            }
        }

        onPressed: mouse => {
            view.takeFocus();
            view.pointerUsed();
            start = Qt.point(mouse.x, mouse.y);
            banding = false;
            if (mouse.button === Qt.LeftButton && !(mouse.modifiers & Qt.ControlModifier)) {
                desktopModel.clearSelection();
            }
        }
        onPositionChanged: mouse => {
            if (!(mouse.buttons & Qt.LeftButton) || !view.iconsShown) {
                return;
            }
            if (!banding && Math.abs(mouse.x - start.x) + Math.abs(mouse.y - start.y) >= Qt.styleHints.startDragDistance) {
                banding = true;
                desktopModel.beginRubberBand(mouse.modifiers & Qt.ControlModifier);
            }
            if (banding) {
                const x = Math.min(start.x, mouse.x);
                const y = Math.min(start.y, mouse.y);
                band = Qt.rect(x, y, Math.abs(mouse.x - start.x), Math.abs(mouse.y - start.y));
                desktopModel.updateRubberBand(Qt.rect(x - icons.x, y - icons.y, band.width, band.height));
            }
        }
        onReleased: {
            if (banding) {
                banding = false;
                desktopModel.endRubberBand();
            }
        }
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                view.openDesktopMenu(mouse.x, mouse.y);
            }
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: drop => view.dropOnDesktop(drop)
    }

    Item {
        id: icons
        x: view.area.x + 10
        y: view.area.y + 16
        width: Math.max(0, view.area.width - 20)
        height: Math.max(0, view.area.height - 24)
        visible: view.iconsShown

        Repeater {
            model: view.iconsShown ? desktopModel : null
            delegate: DesktopIcon {
                view: view
            }
        }
    }

    Rectangle {
        visible: backgroundArea.banding
        x: backgroundArea.band.x
        y: backgroundArea.band.y
        width: backgroundArea.band.width
        height: backgroundArea.band.height
        color: UI.Style.accentAlpha(0.18, 0.18)
        border.width: 1
        border.color: Qt.rgba(UI.Style.accentText.r, UI.Style.accentText.g, UI.Style.accentText.b, 0.6)
    }

    Item {
        id: dragProxy
        Drag.dragType: Drag.Automatic
        Drag.onDragFinished: dropAction => {
            Drag.active = false;
            view.dragging = false;
        }
    }

    // A window of its own, like the taskbar's menus. Drawn inside the
    // desktop's window it sat in the desktop layer, under every open
    // window it overlapped. Outside that window the wallpaper can no longer
    // be blurred through it, so it takes the taskbar menus' glass too.
    UI.Menu {
        id: menu
        popupType: T.Popup.Window
        panelColor: UI.Style.glassMenu
        // Menus stay clear of the taskbar, which is a window of its own
        // above this one.
        topMargin: view.area.y + 6
        leftMargin: view.area.x + 6
        rightMargin: view.width - view.area.x - view.area.width + 6
        bottomMargin: view.height - view.area.y - view.area.height + 6
        onClosed: view.forceActiveFocus()
    }

    Connections {
        target: DeskOps
        function onFailed(message) {
            errorDialog.show(message);
        }
        function onChooseAppRequested(urls) {
            view.chooseApp(urls);
        }
    }

    DialogHost {
        id: promptHost
        transientParent: view.Window.window
        dialog: promptDialog
        PromptDialog {
            id: promptDialog
            host: promptHost
        }
    }

    DialogHost {
        id: errorHost
        transientParent: view.Window.window
        dialog: errorDialog
        ErrorDialog {
            id: errorDialog
            host: errorHost
        }
    }

    DialogHost {
        id: openWithHost
        transientParent: view.Window.window
        dialog: openWithDialog
        OpenWithDialog {
            id: openWithDialog
            host: openWithHost
        }
    }

    DialogHost {
        id: shortcutHost
        transientParent: view.Window.window
        dialog: shortcutDialog
        ShortcutDialog {
            id: shortcutDialog
            host: shortcutHost
        }
    }

    DialogHost {
        id: propertiesHost
        transientParent: view.Window.window
        dialog: propertiesDialog
        PropertiesDialog {
            id: propertiesDialog
            host: propertiesHost
            view: view
        }
    }
}
