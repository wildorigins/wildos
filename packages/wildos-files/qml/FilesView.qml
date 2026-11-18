pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtCore
import WildOS.Files
import WildOS.UI as UI

// Everything inside the Files window, laid out as Main.dc.html and
// FilesDetails.dc.html: title bar with tabs, navigation row, command bar,
// places and content, status bar. Each tab is a FolderModel.
Item {
    id: root

    property var startUrls: []
    // Set when another app asked Files to format a drive.
    property string formatUdi: ""
    // The window's WindowFrame, so maximising goes through it.
    property var frame: null
    property var tabs: []
    property int currentIndex: 0
    readonly property var folder: tabs.length > 0 ? tabs[Math.min(currentIndex, tabs.length - 1)] : null
    readonly property string title: folder ? folder.title : qsTr("Files")
    property bool keyboardUsed: false
    // Whether the keyboard is in the files, not the address bar or a menu.
    readonly property bool contentFocused: content.item !== null && content.item.activeFocus
    property alias menu: menu
    readonly property url home: StandardPaths.writableLocation(StandardPaths.HomeLocation)

    signal closeWindow()

    Component {
        id: folderComponent
        FolderModel {}
    }

    function newTab(url, activate) {
        const model = folderComponent.createObject(root);
        model.renameRequested.connect(row => {
            if (model === root.folder && content.item) {
                content.item.startRename(row);
            }
        });
        model.failed.connect(message => root.showError(message));
        model.openUrl(url && url.toString().length > 0 ? url : root.home);
        const list = tabs.slice();
        const at = tabs.length === 0 ? 0 : currentIndex + 1;
        list.splice(at, 0, model);
        tabs = list;
        if (activate !== false) {
            currentIndex = at;
            focusContent();
        } else if (at <= currentIndex) {
            currentIndex = currentIndex + 1;
        }
        return model;
    }

    // The folders of the tabs closed in this window, latest last, for
    // Ctrl+Shift+T.
    property var closedTabs: []

    function reopenTab() {
        if (closedTabs.length === 0) {
            return;
        }
        const closed = closedTabs.slice();
        const url = closed.pop();
        closedTabs = closed;
        newTab(url);
    }

    function moveTab(from, to) {
        if (from === to || from < 0 || to < 0 || from >= tabs.length || to >= tabs.length) {
            return;
        }
        const current = tabs[currentIndex];
        const list = tabs.slice();
        list.splice(to, 0, list.splice(from, 1)[0]);
        tabs = list;
        currentIndex = list.indexOf(current);
    }

    function showTab(index) {
        if (index >= 0 && index < tabs.length) {
            currentIndex = index;
            focusContent();
        }
    }

    // With "Reopen tabs next time" on, the window's tabs are kept when it
    // closes; Settings ignores this otherwise.
    function saveTabs() {
        Settings.saveTabs(tabs.map(tab => tab.url), currentIndex);
    }

    function closeTab(index) {
        if (tabs.length <= 1) {
            closeWindow();
            return;
        }
        const list = tabs.slice();
        const removed = list.splice(index, 1)[0];
        const closed = closedTabs.concat([removed.url]);
        closedTabs = closed.slice(-10);
        if (index < currentIndex || (index === currentIndex && currentIndex === list.length)) {
            currentIndex = Math.max(0, currentIndex - 1);
        }
        tabs = list;
        removed.destroy();
        focusContent();
    }

    function closeOtherTabs(index) {
        const keep = tabs[index];
        if (!keep) {
            return;
        }
        for (let i = tabs.length - 1; i >= 0; --i) {
            if (tabs[i] !== keep) {
                closeTab(i);
            }
        }
    }

    function closeTabsToRight(index) {
        for (let i = tabs.length - 1; i > index && i > 0; --i) {
            closeTab(i);
        }
    }

    // The copy opens beside its tab and takes over, as in File Explorer.
    function duplicateTab(index) {
        if (index >= 0 && index < tabs.length) {
            currentIndex = index;
            newTab(tabs[index].url);
        }
    }

    // Takes the keyboard from whatever inside the view holds it first, so a
    // click anywhere closes an open name editor and keeps the new name, as
    // clicking away does on Windows. forceActiveFocus() alone hands focus
    // straight back to the editor: each view is a FocusScope.
    function focusContent() {
        const view = content.item;
        if (!view) {
            return;
        }
        const w = root.Window.window;
        const held = w ? w.activeFocusItem : null;
        if (held && held !== view) {
            held.focus = false;
        }
        view.forceActiveFocus();
    }

    function showError(message, detail) {
        if (detail && detail.length > 0) {
            detailDialog.text = message;
            detailDialog.detail = detail;
            detailDialog.shown = false;
            detailDialog.open();
            return;
        }
        errorDialog.text = message;
        errorDialog.open();
    }

    function showMessage(message) {
        toast.show(message);
    }

    Component.onCompleted: {
        let urls = startUrls && startUrls.length > 0 ? startUrls : [];
        let first = 0;
        if (urls.length === 0 && formatUdi.length === 0) {
            urls = Array.from(Settings.takeSavedTabs());
            first = Math.max(0, Math.min(urls.length - 1, Settings.savedTab()));
        }
        if (urls.length === 0) {
            urls = [home];
        }
        for (const url of urls) {
            newTab(url);
        }
        currentIndex = first;
        if (formatUdi.length > 0) {
            Qt.callLater(() => root.formatDrive(root.formatUdi, ""));
        }
    }

    function formatDrive(udi, name) {
        if (!DriveFormat.canFormat(udi)) {
            showError(qsTr("Files only formats removable drives, such as USB sticks and SD cards, and never the drive Wild OS runs from."));
            return;
        }
        // A blank drive is named for what it needs; the dialog says it.
        formatDialog.show(udi, name.replace(qsTr(" (needs formatting)"), ""));
    }

    Connections {
        target: FileOps
        function onCreated(folderUrl, urls, rename) {
            if (FileOps.activeWindow !== root.Window.window || !root.folder) {
                return;
            }
            if (root.folder.url.toString().replace(/\/$/, "") === folderUrl.toString().replace(/\/$/, "")) {
                root.folder.selectWhenListed(urls, rename);
            }
        }
        function onFailed(message) {
            if (FileOps.activeWindow === root.Window.window) {
                root.showError(message);
            }
        }
        function onFailedInDetail(message, detail) {
            if (FileOps.activeWindow === root.Window.window) {
                root.showError(message, detail);
            }
        }
        function onNeedsFormat(udi, name) {
            if (FileOps.activeWindow === root.Window.window) {
                needsFormatDialog.udi = udi;
                needsFormatDialog.kind = name.replace(qsTr(" (needs formatting)"), "");
                needsFormatDialog.open();
            }
        }
        function onChooseAppRequested(urls) {
            if (FileOps.activeWindow === root.Window.window) {
                root.chooseApp(urls);
            }
        }
        function onNotice(message) {
            if (FileOps.activeWindow === root.Window.window) {
                root.showMessage(message);
            }
        }
    }

    // Commands shared by the command bar, the menus and the keyboard.

    function selection() {
        return folder ? folder.selectedUrls() : [];
    }

    function copy() {
        FileOps.copy(selection());
    }

    function cut() {
        FileOps.cut(selection());
    }

    function paste() {
        if (folder && folder.writable) {
            FileOps.paste(folder.url);
        }
    }

    function rename() {
        if (!folder || folder.selectedCount !== 1 || !folder.writable || folder.kind !== "folder") {
            return;
        }
        let row = folder.currentIndex;
        if (!folder.isSelected(row)) {
            row = folder.rowForUrl(selection()[0]);
        }
        if (row >= 0 && content.item) {
            content.item.startRename(row);
        }
    }

    function remove(forGood) {
        const urls = selection();
        if (urls.length === 0 || folder.kind === "computer") {
            return;
        }
        if (forGood || folder.kind === "bin") {
            FileOps.deleteForGood(urls);
        } else {
            FileOps.trash(urls);
        }
    }

    function restore() {
        if (folder.selectedCount > 0) {
            FileOps.restore(selection());
        } else {
            folder.selectAll();
            FileOps.restore(selection());
        }
    }

    function openSelection() {
        if (!folder || folder.selectedCount === 0) {
            return;
        }
        if (folder.selectedCount === 1) {
            let row = folder.currentIndex;
            if (!folder.isSelected(row)) {
                row = folder.rowForUrl(selection()[0]);
            }
            folder.activate(row);
            return;
        }
        const files = [];
        for (const url of selection()) {
            const row = folder.rowForUrl(url);
            if (folder.isDirAt(row)) {
                newTab(url, false);
            } else {
                files.push(url);
            }
        }
        FileOps.open(files);
    }

    function properties(urls) {
        const list = urls && urls.length > 0 ? urls
            : folder.selectedCount > 0 ? selection()
            : folder.kind === "computer" ? []
            : [folder.url];
        if (list.length === 0) {
            return;
        }
        // Each Properties is a window of its own, as on Windows, kept above
        // the Files window it came from. It needs an owner: with none, the
        // JavaScript garbage collector deleted it moments after it opened.
        const window = propertiesComponent.createObject(root,{ view: root, urls: list, transientParent: root.Window.window });
        window.show();
    }

    function chooseApp(urls) {
        openWithDialog.show(urls);
    }

    function dropUrls(drop, target) {
        const urls = drop.urls;
        if (!urls || urls.length === 0) {
            return;
        }
        const action = FileOps.dropAction(urls, target, 0);
        drop.accept(action);
        FileOps.drop(urls, target, action);
    }

    // Menus

    // The layouts in the order and with the keys Windows uses.
    readonly property var layouts: [
        { mode: "xlarge", text: qsTr("Extra large icons"), icon: "view-xlarge" },
        { mode: "large", text: qsTr("Large icons"), icon: "view-large" },
        { mode: "icons", text: qsTr("Medium icons"), icon: "view-medium" },
        { mode: "small", text: qsTr("Small icons"), icon: "view-small" },
        { mode: "list", text: qsTr("List"), icon: "view-list" },
        { mode: "details", text: qsTr("Details"), icon: "list" },
        { mode: "tiles", text: qsTr("Tiles"), icon: "view-tiles" },
        { mode: "content", text: qsTr("Content"), icon: "view-content" }
    ]

    function setLayout(index) {
        if (folder && folder.kind !== "computer" && index >= 0 && index < layouts.length) {
            folder.viewMode = layouts[index].mode;
        }
    }

    // Ctrl and the mouse wheel step through the layouts, larger as the
    // wheel rolls away from you, as in File Explorer. A touchpad sends many
    // small movements, so they add up to a notch first. The change waits
    // for the wheel event to finish, because it replaces the view the
    // event came through.
    property real wheelTravel: 0

    function wheelLayout(delta) {
        if (!folder || folder.kind === "computer") {
            return;
        }
        wheelTravel += delta;
        if (Math.abs(wheelTravel) < 120) {
            return;
        }
        const step = wheelTravel > 0 ? -1 : 1;
        wheelTravel = 0;
        const at = layouts.findIndex(l => l.mode === folder.viewMode);
        const next = Math.max(0, Math.min(layouts.length - 1, at + step));
        if (at >= 0 && next !== at) {
            Qt.callLater(() => root.setLayout(next));
        }
    }

    // This computer has one layout, its folders and drive cards, so the
    // layouts are shown but cannot be picked there.
    function viewEntries() {
        const mode = folder.viewMode;
        const fixed = folder.kind === "computer";
        return layouts.map((l, i) => ({
            text: l.text,
            icon: l.icon,
            radio: true,
            enabled: !fixed,
            checked: !fixed && mode === l.mode,
            shortcut: qsTr("Ctrl+Shift+%1").arg(i + 1),
            run: () => root.setLayout(i)
        }));
    }

    function showEntries() {
        return [
            { text: qsTr("Details pane"), icon: "pane", checked: Settings.detailsPane, run: () => Settings.detailsPane = !Settings.detailsPane },
            { text: qsTr("File name extensions"), icon: "extension", checked: Settings.showExtensions, run: () => Settings.showExtensions = !Settings.showExtensions },
            { text: qsTr("Hidden items"), icon: "hidden", shortcut: "Ctrl+H", checked: Settings.showHidden, run: () => Settings.showHidden = !Settings.showHidden }
        ];
    }

    function sortEntries() {
        const titles = folder.columnTitles;
        const entries = [];
        for (let i = 0; i < 4; ++i) {
            const column = i;
            entries.push({ text: titles[i], radio: true, checked: folder.sortColumn === i, run: () => root.folder.sortColumn = column });
        }
        entries.push({ separator: true });
        entries.push({ text: qsTr("Ascending"), radio: true, checked: !folder.sortDescending, run: () => root.folder.sortDescending = false });
        entries.push({ text: qsTr("Descending"), radio: true, checked: folder.sortDescending, run: () => root.folder.sortDescending = true });
        return entries;
    }

    function newEntries() {
        const entries = [
            { text: qsTr("Folder"), icon: "new-folder", shortcut: qsTr("Ctrl+Shift+N"), run: () => FileOps.newFolder(root.folder.url) },
            { text: qsTr("Shortcut"), icon: "link", run: () => shortcutDialog.show(root.folder.url) },
            { separator: true }
        ];
        for (const type of FileOps.newFileTypes()) {
            const id = type.id;
            entries.push({ text: type.text, themeIcon: type.icon + ",text-x-generic", run: () => FileOps.newFile(root.folder.url, id) });
        }
        return entries;
    }

    // Each menu has the width File Explorer gives it, wider with larger
    // text or longer names. A menu opened from the keyboard starts on its
    // first row, as Explorer's does.
    function openMenu(entries, anchorItem, below, x, y, width, quickActions) {
        const w = Math.round((width || 248) * Math.max(1, UI.Style.textScale));
        menu.close();
        menu.minimumWidth = w;
        menu.maximumWidth = Math.max(380, w);
        // Ticks and dots get their own column only in menus that have them.
        menu.markColumn = entries.some(e => e.checked !== undefined);
        menu.quickActions = quickActions || [];
        menu.entries = entries;
        if (below) {
            menu.popup(anchorItem, 0, anchorItem.height + 4);
        } else {
            menu.popup(anchorItem, x, y);
        }
        if (keyboardUsed) {
            for (let i = 0; i < entries.length; ++i) {
                if (!entries[i].separator && entries[i].enabled !== false) {
                    menu.currentIndex = i;
                    break;
                }
            }
        }
    }

    function openNewMenu(anchorItem) {
        openMenu(newEntries(), anchorItem, true);
    }

    function openSortMenu(anchorItem) {
        openMenu(sortEntries(), anchorItem, true, 0, 0, 200);
    }

    function openViewMenu(anchorItem) {
        const entries = viewEntries();
        entries.push({ separator: true });
        entries.push({ text: qsTr("Show"), icon: "hidden", submenu: showEntries() });
        openMenu(entries, anchorItem, true, 0, 0, 296);
    }

    function openMoreMenu(anchorItem) {
        const entries = [
            { text: qsTr("Select all"), icon: "select-all", shortcut: "Ctrl+A", run: () => root.folder.selectAll() },
            { text: qsTr("Select none"), icon: "select-none", enabled: folder.selectedCount > 0, run: () => root.folder.clearSelection() },
            { text: qsTr("Invert selection"), icon: "invert", run: () => root.folder.invertSelection() },
            { separator: true },
            { text: qsTr("Copy as path"), icon: "link", enabled: folder.kind === "folder", run: () => FileOps.copyPaths(root.folder.selectedCount > 0 ? root.selection() : [root.folder.url]) }
        ];
        if (folder.url.toString().startsWith("file:")) {
            entries.push({ text: qsTr("Open in Terminal"), icon: "terminal", run: () => FileOps.openTerminal(root.folder.url) });
        }
        entries.push({ text: qsTr("Open new window"), icon: "new-window", shortcut: "Ctrl+N", run: () => App.openWindow([root.folder.url]) });
        entries.push({ separator: true });
        entries.push({ text: qsTr("Properties"), icon: "info", shortcut: qsTr("Alt+Enter"), enabled: folder.kind !== "computer" || folder.selectedCount > 0, run: () => root.properties() });
        entries.push({ separator: true });
        entries.push({ text: qsTr("Reopen closed tab"), icon: "restore", shortcut: "Ctrl+Shift+T", enabled: closedTabs.length > 0, run: () => root.reopenTab() });
        entries.push({ text: qsTr("Reopen tabs next time"), icon: "new-tab", checked: Settings.restoreTabs, run: () => Settings.restoreTabs = !Settings.restoreTabs });
        entries.push({ separator: true });
        entries.push({ text: qsTr("Report a problem"), icon: "warning", run: () => Qt.openUrlExternally("wildos-report:files") });
        openMenu(entries, anchorItem, true, 0, 0, 252);
    }

    function openTabMenu(index, anchorItem, x, y) {
        if (index < 0 || index >= tabs.length) {
            return;
        }
        openMenu([
            { text: qsTr("Close tab"), icon: "close", shortcut: "Ctrl+W", run: () => root.closeTab(index) },
            { text: qsTr("Close other tabs"), enabled: tabs.length > 1, run: () => root.closeOtherTabs(index) },
            { text: qsTr("Close tabs to the right"), enabled: index < tabs.length - 1, run: () => root.closeTabsToRight(index) },
            { separator: true },
            { text: qsTr("Duplicate tab"), icon: "duplicate", run: () => root.duplicateTab(index) },
            { text: qsTr("Reopen closed tab"), icon: "restore", shortcut: "Ctrl+Shift+T", enabled: closedTabs.length > 0, run: () => root.reopenTab() }
        ], anchorItem, false, x, y, 260);
    }

    function openShareMenu(anchorItem) {
        const targets = FileOps.shareTargets(selection());
        const entries = targets.map(t => ({ text: t.name, themeIcon: t.icon + ",document-share", run: () => FileOps.share(t.row) }));
        if (entries.length > 0) {
            entries.push({ separator: true });
        }
        entries.push({ text: qsTr("Copy as path"), icon: "link", run: () => FileOps.copyPaths(root.selection()) });
        openMenu(entries, anchorItem, true, 0, 0, 240);
    }

    function openItemMenu(item, x, y) {
        const f = folder;
        const urls = selection();
        const one = f.selectedCount === 1;
        const row = one && urls.length > 0 ? f.rowForUrl(urls[0]) : -1;
        const isDir = one && f.isDirAt(row);
        const entries = [];

        if (f.kind === "bin") {
            entries.push({ text: qsTr("Restore"), icon: "restore", run: () => FileOps.restore(urls) });
            entries.push({ text: qsTr("Delete for good"), icon: "delete", danger: true, shortcut: qsTr("Delete"), run: () => FileOps.deleteForGood(urls) });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Properties"), icon: "info", shortcut: qsTr("Alt+Enter"), run: () => root.properties(urls) });
            openMenu(entries, item, false, x, y, 236);
            return;
        }

        if (f.kind === "computer") {
            // Drives are not in the selection's URLs; the current card is.
            const at = f.currentIndex;
            const udi = f.udiAt(at);
            const driveUrl = f.urlAt(at);
            const opened = driveUrl.toString().length > 0;
            entries.push({ text: qsTr("Open"), icon: "open", run: () => f.activate(at) });
            entries.push({ text: qsTr("Open in new tab"), icon: "new-tab", enabled: opened, run: () => root.newTab(driveUrl, false) });
            if (udi.length > 0 && f.isRemovableAt(at)) {
                entries.push({ text: qsTr("Eject"), icon: "eject", enabled: opened, run: () => FileOps.ejectDrive(udi) });
                if (DriveFormat.canFormat(udi)) {
                    const name = f.nameAt(at);
                    entries.push({ text: qsTr("Format…"), icon: "drive", run: () => root.formatDrive(udi, name) });
                }
            }
            entries.push({ separator: true });
            entries.push({ text: qsTr("Properties"), icon: "info", enabled: opened, run: () => root.properties([driveUrl]) });
            openMenu(entries, item, false, x, y, 236);
            return;
        }

        // DialogFileMenu.dc.html: the everyday actions as icons along the
        // top, as in Windows 11, then Open, Open with, Extract here (for
        // archives), Compress to ZIP file and Compress...,
        // Send to, Copy as path, Pin to Start and Properties.
        const local = urls.length > 0 && urls.every(u => u.toString().startsWith("file:"));
        const quick = [
            { icon: "cut", tip: qsTr("Cut (Ctrl+X)"), enabled: f.writable, run: () => root.cut() },
            { icon: "copy", tip: qsTr("Copy (Ctrl+C)"), run: () => root.copy() },
            { icon: "rename", tip: qsTr("Rename (F2)"), enabled: one && f.writable, run: () => root.rename() },
            { icon: "share", tip: qsTr("Share"), enabled: !f.selectionHasFolders(), run: () => root.openShareMenuAt(item, x, y) },
            { icon: "delete", tip: qsTr("Delete (Delete)"), enabled: f.writable, run: () => root.remove(false) }
        ];
        entries.push({ text: qsTr("Open"), icon: one && !isDir ? "" : "open", themeIcon: one && !isDir ? f.iconAt(row) : undefined, shortcut: qsTr("Enter"), run: () => root.openSelection() });
        if (isDir) {
            entries.push({ text: qsTr("Open in new tab"), icon: "new-tab", run: () => root.newTab(urls[0], false) });
            entries.push({ text: qsTr("Open in new window"), icon: "new-window", run: () => App.openWindow(urls) });
        } else if (one) {
            // The default app first, Windows apps from App Bridge labelled as
            // such, as the board shows.
            const apps = FileOps.appsFor(urls[0]).map(a => ({
                text: a.name,
                themeIcon: a.icon + ",application-x-executable",
                shortcut: a.isDefault ? qsTr("Default") : a.windows ? qsTr("Windows app") : "",
                run: () => FileOps.openWith(urls, a.id, false)
            }));
            if (apps.length > 0) {
                apps.push({ separator: true });
            }
            apps.push({ text: qsTr("Choose another app…"), run: () => root.chooseApp(urls) });
            entries.push({ text: qsTr("Open with"), icon: "open-with", submenu: apps });
            // An app shortcut or a program such as an AppImage, on a PC with
            // two graphics cards.
            const target = urls[0].toString();
            if (local && UI.System.twoGraphicsCards() && UI.System.canChooseGraphics(target)) {
                const dedicated = UI.System.dedicatedGraphics(target);
                entries.push({ text: qsTr("Dedicated graphics card"), checked: dedicated,
                    run: () => UI.System.setDedicatedGraphics(target, !dedicated) });
            }
            // Advanced protection's Safe box, while it is switched on.
            if (local && FileOps.safeBoxPlan(urls[0]).ok) {
                entries.push({ text: qsTr("Open in safe box"), icon: "sandbox", run: () => FileOps.openInSafeBox(urls[0]) });
            }
        }
        if (local && FileOps.areArchives(urls)) {
            entries.push({ text: qsTr("Extract here"), icon: "zip", enabled: FileOps.canWriteIn(f.url), run: () => FileOps.extractHere(urls) });
        }
        if (local) {
            entries.push({ text: qsTr("Compress to ZIP file"), icon: "zip", enabled: FileOps.canWriteIn(f.url), run: () => FileOps.compress(urls) });
            if (FileOps.canUseArchive()) {
                entries.push({ text: qsTr("Compress…"), icon: "zip", run: () => FileOps.compressWith(urls) });
            }
            // Advanced protection's Remove personal details, while it is
            // switched on, for the kinds of file it can clean.
            if (FileOps.canRemoveDetails(urls)) {
                entries.push({ text: qsTr("Remove personal details"), icon: "eye-off", run: () => FileOps.removeDetails(urls) });
            }
            const targets = FileOps.sendTargets(urls).map(t => ({
                text: t.text,
                icon: t.icon ?? "",
                themeIcon: t.themeIcon,
                run: () => FileOps.sendTo(urls, t.kind, t.target ?? "")
            }));
            if (targets.length > 0) {
                entries.push({ text: qsTr("Send to"), icon: "send", submenu: targets });
            }
        }
        entries.push({ text: qsTr("Copy as path"), icon: "link", shortcut: qsTr("Ctrl+Shift+C"), run: () => FileOps.copyPaths(urls) });
        if (local && FileOps.canPinToStart()) {
            entries.push({ text: qsTr("Pin to Start"), icon: "pin", run: () => FileOps.pinToStart(urls) });
        }
        // Network folders (a NAS share) pin too; canPin says which.
        if (isDir) {
            if (PlacesModel.isPinned(urls[0])) {
                entries.push({ text: qsTr("Unpin from sidebar"), icon: "unpin", run: () => PlacesModel.unpin(urls[0]) });
            } else if (PlacesModel.canPin(urls[0])) {
                entries.push({ text: qsTr("Pin to sidebar"), icon: "pin", run: () => PlacesModel.pin(urls[0]) });
            }
        }
        if (isDir && local) {
            entries.push({ text: qsTr("Open in Terminal"), icon: "terminal", run: () => FileOps.openTerminal(urls[0]) });
        }
        entries.push({ separator: true });
        entries.push({ text: qsTr("Properties"), icon: "properties", shortcut: qsTr("Alt+Enter"), run: () => root.properties(urls) });
        openMenu(entries, item, false, x, y, 270, quick);
    }

    function openShareMenuAt(item, x, y) {
        const targets = FileOps.shareTargets(selection());
        const entries = targets.map(t => ({ text: t.name, themeIcon: t.icon + ",document-share", run: () => FileOps.share(t.row) }));
        if (entries.length > 0) {
            entries.push({ separator: true });
        }
        entries.push({ text: qsTr("Copy as path"), icon: "link", run: () => FileOps.copyPaths(root.selection()) });
        openMenu(entries, item, false, x, y, 240);
    }

    // MenusApps.dc "Files, empty space in a folder": new items and Paste
    // first, then View and Sort by, then Terminal and Properties. New holds
    // the file types and Shortcut, as New folder has a row of its own.
    function openFolderMenu(item, x, y) {
        const f = folder;
        if (f.kind === "folder") {
            const types = FileOps.newFileTypes().map(type => {
                const id = type.id;
                return { text: type.text, themeIcon: type.icon + ",text-x-generic", run: () => FileOps.newFile(root.folder.url, id) };
            });
            if (types.length > 0) {
                types.push({ separator: true });
            }
            types.push({ text: qsTr("Shortcut"), icon: "link", run: () => shortcutDialog.show(root.folder.url) });
            const local = f.url.toString().startsWith("file:");
            const rows = [
                { text: qsTr("New folder"), icon: "new-folder", shortcut: qsTr("Ctrl+Shift+N"), enabled: f.writable, run: () => FileOps.newFolder(root.folder.url) },
                { text: qsTr("New"), icon: "plus", enabled: f.writable, submenu: types, submenuWidth: 168 },
                { text: qsTr("Paste"), icon: "menu-paste", shortcut: "Ctrl+V", enabled: FileOps.canPaste && f.writable, run: () => root.paste() },
                { text: FileOps.undoText, icon: "undo", shortcut: "Ctrl+Z", enabled: FileOps.canUndo, run: () => FileOps.undo() },
                { separator: true },
                { text: qsTr("View"), icon: "desktop-view", submenu: viewEntries() },
                { text: qsTr("Sort by"), icon: "menu-sort", submenu: sortEntries() },
                { separator: true }
            ];
            if (local) {
                rows.push({ text: qsTr("Open in Terminal"), icon: "terminal", run: () => FileOps.openTerminal(root.folder.url) });
            }
            if (PlacesModel.isPinned(f.url)) {
                rows.push({ text: qsTr("Unpin from sidebar"), icon: "unpin", run: () => PlacesModel.unpin(root.folder.url) });
            } else if (PlacesModel.canPin(f.url)) {
                rows.push({ text: qsTr("Pin to sidebar"), icon: "pin", run: () => PlacesModel.pin(root.folder.url) });
            }
            rows.push({ text: qsTr("Properties"), icon: "properties", shortcut: qsTr("Alt+Enter"), run: () => root.properties([root.folder.url]) });
            openMenu(rows, item, false, x, y, 236);
            return;
        }
        const entries = [
            { text: qsTr("View"), icon: "view", enabled: f.kind !== "computer", submenu: viewEntries() },
            { text: qsTr("Sort by"), icon: "sort", enabled: f.kind !== "computer", submenu: sortEntries() },
            { text: qsTr("Refresh"), icon: "refresh", shortcut: "F5", run: () => root.folder.refresh() },
            { separator: true }
        ];
        if (f.kind === "bin") {
            entries.push({ text: qsTr("Empty Recycle Bin"), icon: "bin", enabled: FileOps.binCount > 0, run: () => FileOps.emptyBin() });
            entries.push({ text: qsTr("Restore all"), icon: "restore", enabled: f.count > 0, run: () => root.restore() });
        }
        if (entries[entries.length - 1].separator) {
            entries.pop();
        }
        openMenu(entries, item, false, x, y, 280);
    }

    // MenusApps.dc "Files, sidebar folder" and MenusShell.dc "Bin". Only
    // a folder you pinned yourself has "Unpin from sidebar"; the Home
    // folders stay. The Recycle Bin has no Properties of its own.
    function openPlaceMenu(item, x, y, place) {
        const entries = [];
        const hasUrl = place.url.toString().length > 0;
        entries.push({ text: qsTr("Open"), icon: "open", bold: true, run: () => sidebar.open(place, false) });
        if (place.kind === "bin") {
            entries.push({ separator: true });
            entries.push({ text: qsTr("Empty Recycle Bin"), icon: "menu-uninstall", danger: true, enabled: FileOps.binCount > 0, run: () => FileOps.emptyBin() });
            openMenu(entries, item, false, x, y, 280);
            return;
        }
        if (hasUrl) {
            entries.push({ text: qsTr("Open in new window"), icon: "menu-window", run: () => App.openWindow([place.url]) });
        }
        entries.push({ text: qsTr("Open in new tab"), icon: "plus", run: () => sidebar.open(place, true) });
        const formattable = place.removable && place.udi.length > 0 && DriveFormat.canFormat(place.udi);
        if ((place.removable && hasUrl) || formattable) {
            entries.push({ separator: true });
        }
        if (place.removable && hasUrl) {
            entries.push({ text: qsTr("Eject"), icon: "eject", run: () => FileOps.ejectDrive(place.udi) });
        }
        if (formattable) {
            entries.push({ text: qsTr("Format…"), icon: "drive", run: () => root.formatDrive(place.udi, place.name) });
        }
        if (place.kind === "pinned") {
            const pinned = place.url;
            entries.push({ separator: true });
            entries.push({ text: qsTr("Unpin from sidebar"), icon: "unpin", run: () => PlacesModel.unpin(pinned) });
        }
        if (place.kind === "place" || place.kind === "pinned" || (place.kind === "drive" && hasUrl)) {
            const target = place.url;
            entries.push({ separator: true });
            entries.push({ text: qsTr("Properties"), icon: "properties", run: () => root.properties([target]) });
        }
        openMenu(entries, item, false, x, y, 280);
    }

    function showScope(anchorItem) {
        scopeCard.title = folder.scope;
        scopeCard.detail = folder.scopeDetail;
        const p = anchorItem.mapToItem(root, anchorItem.width, anchorItem.height + 6);
        scopeCard.anchorX = p.x;
        scopeCard.anchorY = p.y;
        scopeCard.shown = true;
        scopeCard.forceActiveFocus();
    }

    // Keyboard, as in Explorer.

    property string typed: ""
    Timer {
        id: typedTimer
        interval: 1000
        onTriggered: root.typed = ""
    }

    function handleKey(event, view) {
        const f = folder;
        if (!f) {
            return;
        }
        const mods = event.modifiers;
        const ctrl = mods & Qt.ControlModifier;
        const shift = mods & Qt.ShiftModifier;
        const alt = mods & Qt.AltModifier;
        const cols = view.columns();
        const across = view.across ? view.across() : (cols > 1 ? 1 : 0);
        const n = f.count;
        const cur = f.currentIndex;
        const move = row => {
            keyboardUsed = true;
            if (n > 0) {
                f.moveCurrent(Math.max(0, Math.min(n - 1, row)), mods);
            }
        };
        switch (event.key) {
        case Qt.Key_Left:
            if (across > 0 && !alt) {
                move(cur < 0 ? 0 : cur - across);
            } else {
                return;
            }
            break;
        case Qt.Key_Right:
            if (across > 0 && !alt) {
                move(cur < 0 ? 0 : cur + across);
            } else {
                return;
            }
            break;
        case Qt.Key_Up:
            if (alt) {
                return;
            }
            move(cur < 0 ? 0 : cur - cols);
            break;
        case Qt.Key_Down:
            move(cur < 0 ? 0 : cur + cols);
            break;
        case Qt.Key_Home:
            move(0);
            break;
        case Qt.Key_End:
            move(n - 1);
            break;
        case Qt.Key_PageUp:
            move(cur - view.pageRows());
            break;
        case Qt.Key_PageDown:
            move(cur + view.pageRows());
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (alt) {
                properties();
            } else {
                openSelection();
            }
            break;
        case Qt.Key_Backspace:
            f.back();
            break;
        case Qt.Key_Delete:
            remove(shift);
            break;
        case Qt.Key_F2:
            rename();
            break;
        case Qt.Key_Escape:
            f.clearSelection();
            break;
        case Qt.Key_Space:
            // Ctrl+Space toggles the item, as in Explorer; a space in the
            // middle of typing a name is part of the name; otherwise Space
            // is Quick Look.
            if (ctrl && cur >= 0) {
                f.select(cur, Qt.ControlModifier);
            } else if (root.typed.length > 0) {
                typeAhead(" ");
            } else if (f.kind === "computer") {
                if (cur >= 0 && !f.isSelected(cur)) {
                    f.select(cur, 0);
                }
            } else {
                quickLook();
            }
            break;
        case Qt.Key_Menu:
            openContextAtCurrent(view);
            break;
        case Qt.Key_F10:
            if (shift) {
                openContextAtCurrent(view);
            } else {
                return;
            }
            break;
        case Qt.Key_A:
            if (ctrl) {
                f.selectAll();
            } else {
                typeAhead(event.text);
            }
            break;
        case Qt.Key_C:
            if (ctrl) {
                copy();
            } else {
                typeAhead(event.text);
            }
            break;
        case Qt.Key_X:
            if (ctrl) {
                cut();
            } else {
                typeAhead(event.text);
            }
            break;
        case Qt.Key_V:
            if (ctrl) {
                paste();
            } else {
                typeAhead(event.text);
            }
            break;
        case Qt.Key_Z:
            if (ctrl) {
                FileOps.undo();
            } else {
                typeAhead(event.text);
            }
            break;
        default:
            if (ctrl || alt || event.text.length === 0 || event.text.charCodeAt(0) < 32) {
                return;
            }
            typeAhead(event.text);
            break;
        }
        event.accepted = true;
    }

    // Typing a name jumps to it, as in Explorer.
    function typeAhead(text) {
        if (text.length === 0) {
            return;
        }
        const f = folder;
        const repeat = typed.length === 1 && text.toLowerCase() === typed.toLowerCase();
        typed = repeat ? typed : typed + text;
        typedTimer.restart();
        const from = repeat || typed.length === 1 ? f.currentIndex + 1 : Math.max(0, f.currentIndex);
        const row = f.findPrefix(typed, Math.max(0, from));
        if (row >= 0) {
            keyboardUsed = true;
            f.select(row, 0);
        }
    }

    // Quick Look shows the item with the keyboard focus when it is selected,
    // the first selected item otherwise. With nothing selected, Space picks
    // the focused item first, as it did before Quick Look.
    property var quickLookWindow: null

    function quickLook() {
        const f = folder;
        let row = f.currentIndex;
        if (row < 0 || !f.isSelected(row)) {
            if (f.selectedCount > 0) {
                row = f.rowForUrl(selection()[0]);
            } else if (row >= 0) {
                f.select(row, 0);
            }
        }
        if (row < 0) {
            return;
        }
        if (!quickLookWindow) {
            quickLookWindow = quickLookComponent.createObject(root, { transientParent: root.Window.window });
        }
        quickLookWindow.show(f.urlAt(row));
    }

    // The arrow keys while Quick Look is open: Left and Right step through
    // the items in order whatever the layout, Up and Down by a row.
    function quickLookStep(delta, vertical) {
        const f = folder;
        if (!f || f.count === 0) {
            return;
        }
        const rowSize = vertical && content.item ? content.item.columns() : 1;
        const cur = f.currentIndex < 0 ? 0 : f.currentIndex;
        keyboardUsed = true;
        f.moveCurrent(Math.max(0, Math.min(f.count - 1, cur + delta * rowSize)), 0);
    }

    function openContextAtCurrent(view) {
        if (folder.selectedCount === 0) {
            openFolderMenu(view, 24, 24);
            return;
        }
        const r = view.itemRect(folder.currentIndex >= 0 ? folder.currentIndex : 0);
        openItemMenu(view, r.x + 24, r.y + r.height / 2);
    }

    // Layout

    Column {
        anchors.fill: parent

        TitleBar {
            id: titleBar
            width: parent.width
            view: root
            frame: root.frame
        }

        NavBar {
            id: nav
            width: parent.width
            folder: root.folder
            view: root
        }

        CommandBar {
            width: parent.width
            folder: root.folder
            view: root
        }

        Item {
            width: parent.width
            height: parent.height - titleBar.height - nav.height - Metrics.commandHeight - status.height

            Sidebar {
                id: sidebar
                height: parent.height
                // Above the files, so the grip on its edge can be caught
                // from either side of the line.
                z: 1
                folder: root.folder
                view: root
            }

            Loader {
                id: content
                anchors.left: sidebar.right
                anchors.right: pane.visible ? pane.left : parent.right
                height: parent.height
                focus: true
                sourceComponent: !root.folder ? null : root.folder.kind === "computer" ? computerComponent : root.folder.viewMode === "details" ? detailsComponent : root.folder.viewMode === "content" ? contentComponent : iconsComponent
                onLoaded: if (root.Window.window && root.Window.window.active) item.forceActiveFocus()
            }

            DetailsPane {
                id: pane
                visible: Settings.detailsPane && parent.width > 640
                anchors.right: parent.right
                height: parent.height
                folder: root.folder
            }
        }

        StatusBar {
            id: status
            width: parent.width
            folder: root.folder
        }
    }

    Component {
        id: detailsComponent
        DetailsView {
            folder: root.folder
            view: root
        }
    }

    Component {
        id: iconsComponent
        IconsView {
            folder: root.folder
            view: root
            mode: root.folder ? root.folder.viewMode : "icons"
        }
    }

    Component {
        id: computerComponent
        ComputerView {
            folder: root.folder
            view: root
        }
    }

    Component {
        id: contentComponent
        ContentView {
            folder: root.folder
            view: root
        }
    }

    // Ctrl+Shift+1 to 8 pick the layouts, as in File Explorer.
    Shortcut {
        sequences: ["Ctrl+Shift+1"]
        onActivated: root.setLayout(0)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+2"]
        onActivated: root.setLayout(1)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+3"]
        onActivated: root.setLayout(2)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+4"]
        onActivated: root.setLayout(3)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+5"]
        onActivated: root.setLayout(4)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+6"]
        onActivated: root.setLayout(5)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+7"]
        onActivated: root.setLayout(6)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+8"]
        onActivated: root.setLayout(7)
    }

    // Window-wide keys. Keys that act on files live in the views, so they
    // never fire while typing in the address bar or a name.
    Shortcut {
        sequences: ["Ctrl+T"]
        onActivated: root.newTab(root.folder ? root.folder.url : root.home)
    }
    Shortcut {
        sequences: ["Ctrl+W", "Ctrl+F4"]
        onActivated: root.closeTab(root.currentIndex)
    }
    Shortcut {
        sequences: ["Ctrl+Tab", "Ctrl+PgDown"]
        onActivated: root.currentIndex = (root.currentIndex + 1) % root.tabs.length
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab", "Ctrl+PgUp"]
        onActivated: root.currentIndex = (root.currentIndex + root.tabs.length - 1) % root.tabs.length
    }
    Shortcut {
        sequences: ["Ctrl+Shift+T"]
        onActivated: root.reopenTab()
    }
    // Ctrl+Shift+Page Up and Page Down move the tab along the bar, and
    // Ctrl+1 to 8 pick a tab by its place, Ctrl+9 the last, as browsers do.
    Shortcut {
        sequences: ["Ctrl+Shift+PgUp"]
        onActivated: root.moveTab(root.currentIndex, root.currentIndex - 1)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+PgDown"]
        onActivated: root.moveTab(root.currentIndex, root.currentIndex + 1)
    }
    Shortcut {
        sequences: ["Ctrl+1"]
        onActivated: root.showTab(0)
    }
    Shortcut {
        sequences: ["Ctrl+2"]
        onActivated: root.showTab(1)
    }
    Shortcut {
        sequences: ["Ctrl+3"]
        onActivated: root.showTab(2)
    }
    Shortcut {
        sequences: ["Ctrl+4"]
        onActivated: root.showTab(3)
    }
    Shortcut {
        sequences: ["Ctrl+5"]
        onActivated: root.showTab(4)
    }
    Shortcut {
        sequences: ["Ctrl+6"]
        onActivated: root.showTab(5)
    }
    Shortcut {
        sequences: ["Ctrl+7"]
        onActivated: root.showTab(6)
    }
    Shortcut {
        sequences: ["Ctrl+8"]
        onActivated: root.showTab(7)
    }
    Shortcut {
        sequences: ["Ctrl+9"]
        onActivated: root.showTab(root.tabs.length - 1)
    }
    Shortcut {
        sequences: ["Ctrl+N"]
        onActivated: App.openWindow([root.folder.url])
    }
    Shortcut {
        sequences: ["Ctrl+Shift+N"]
        onActivated: if (root.folder.writable) FileOps.newFolder(root.folder.url)
    }
    Shortcut {
        sequences: ["Ctrl+L", "Alt+D", "F4"]
        onActivated: nav.address.edit()
    }
    Shortcut {
        sequences: ["Ctrl+F", "Ctrl+E", "F3"]
        onActivated: {
            nav.search.forceActiveFocus();
            nav.search.selectAll();
        }
    }
    Shortcut {
        sequences: ["Alt+Left"]
        onActivated: root.folder.back()
    }
    Shortcut {
        sequences: ["Alt+Right"]
        onActivated: root.folder.forward()
    }
    Shortcut {
        sequences: ["Alt+Up"]
        onActivated: root.folder.up()
    }
    Shortcut {
        sequences: ["F5", "Ctrl+R"]
        onActivated: root.folder.refresh()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+C"]
        onActivated: if (root.folder && root.folder.selectedCount > 0) FileOps.copyPaths(root.selection())
    }
    Shortcut {
        sequences: ["Ctrl+H"]
        onActivated: Settings.showHidden = !Settings.showHidden
    }
    // F6 moves the keyboard between the places and the files, as in
    // File Explorer.
    Shortcut {
        sequences: ["F6", "Shift+F6"]
        onActivated: sidebar.focused ? root.focusContent() : sidebar.takeFocus()
    }

    // Mouse back and forward buttons.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onClicked: mouse => mouse.button === Qt.BackButton ? root.folder.back() : root.folder.forward()
    }

    // Overlays

    UI.Menu {
        id: menu
        markColumn: true
    }

    FocusScope {
        id: scopeCard
        property string title
        property string detail
        property real anchorX
        property real anchorY
        property bool shown: false
        anchors.fill: parent
        // Stays until it has faded out.
        visible: shown || scopeFade.running
        z: 950
        Keys.onEscapePressed: shown = false
        onShownChanged: if (!shown) root.focusContent()

        MouseArea {
            anchors.fill: parent
            enabled: scopeCard.shown
            onPressed: scopeCard.shown = false
        }

        Rectangle {
            x: Math.max(8, scopeCard.anchorX - width)
            y: scopeCard.anchorY
            width: 300
            height: scopeColumn.implicitHeight + 32
            radius: UI.Style.radiusMd
            color: UI.Style.glassMenu
            border.width: 1
            border.color: UI.Style.borderMenu
            opacity: scopeCard.shown ? 1 : 0
            Behavior on opacity {
                NumberAnimation {
                    id: scopeFade
                    duration: UI.Style.motionFast
                    easing.type: Easing.OutCubic
                }
            }
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: UI.Style.shadowMenu
                shadowVerticalOffset: 12
                shadowBlur: 1.0
                blurMax: 32
            }

            Column {
                id: scopeColumn
                x: 16
                y: 16
                width: parent.width - 32
                spacing: 8

                Row {
                    spacing: 8
                    UI.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "shield"
                        size: 16
                        strokeWidth: 2
                        color: UI.Style.accent
                    }
                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: scopeCard.title
                        font.weight: UI.Style.weightSemiBold
                    }
                }
                UI.Label {
                    width: parent.width
                    text: scopeCard.detail
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    lineHeight: 1.3
                }
            }
        }
    }

    UI.Dialog {
        id: errorDialog
        title: qsTr("That did not work")
        onClosed: root.focusContent()
        buttons: [
            UI.Button {
                kind: "primary"
                text: qsTr("Close")
                onClicked: errorDialog.close()
            }
        ]
    }

    // A failure Files has only a general sentence for: what the system
    // reported waits behind "Details", for whoever is asked to help.
    UI.Dialog {
        id: detailDialog
        property string detail
        property bool shown: false
        title: qsTr("That did not work")
        onClosed: root.focusContent()

        UI.LinkButton {
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: -4
            visible: !detailDialog.shown
            text: qsTr("Details")
            onClicked: detailDialog.shown = true
        }

        UI.Label {
            Layout.fillWidth: true
            visible: detailDialog.shown
            text: detailDialog.detail
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                kind: "primary"
                text: qsTr("Close")
                onClicked: detailDialog.close()
            }
        ]
    }

    PromptDialog {
        mine: FileOps.activeWindow === root.Window.window
        onClosed: root.focusContent()
    }

    Component {
        id: propertiesComponent
        PropertiesWindow {}
    }

    Component {
        id: quickLookComponent
        QuickLook {
            view: root
        }
    }

    // Opening a blank drive, as Windows asks "You need to format the disk
    // before you can use it".
    UI.Dialog {
        id: needsFormatDialog
        property string udi
        property string kind
        readonly property string lower: /^(USB|SD) /.test(kind) ? kind : kind.charAt(0).toLowerCase() + kind.slice(1)
        title: qsTr("Format this %1?").arg(lower)
        text: qsTr("This %1 has nothing on it Wild OS can read, so it needs formatting before it can be used. Formatting erases anything on it.").arg(lower)
        onClosed: root.focusContent()
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: needsFormatDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Format…")
                onClicked: {
                    needsFormatDialog.close();
                    root.formatDrive(needsFormatDialog.udi, needsFormatDialog.kind);
                }
            }
        ]
    }

    FormatDialog {
        id: formatDialog
        onClosed: root.focusContent()
        onOpenDrive: path => root.folder.openUrl("file://" + path)
    }

    ShortcutDialog {
        id: shortcutDialog
        onClosed: root.focusContent()
    }

    OpenWithDialog {
        id: openWithDialog
        onClosed: root.focusContent()
    }

    UI.Toast {
        id: toast
        z: 800
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Metrics.statusHeight + 16
    }
}
