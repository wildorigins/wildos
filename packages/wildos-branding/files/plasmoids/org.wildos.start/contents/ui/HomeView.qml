pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI

// Pinned apps and recent documents, the page Start opens on, and the Office
// apps Windows mode is getting ready while it sets itself up (board 9.13).
FocusScope {
    id: home

    required property var launcher

    property alias allAppsButton: pinnedHeader.button
    property alias pinnedGrid: pinnedGrid
    property alias moreButton: recentHeader.button
    property alias recentGrid: recentGrid
    readonly property bool hasRecents: launcher.recentModel.count > 0
    // "More" swaps the pinned apps for the whole recent list, as Windows'
    // Recommended page does; it is only offered when there is more to see.
    property bool showAllRecents: false
    readonly property bool moreRecents: launcher.recentModel.count > 6
    // The page's height with the pinned apps showing. "More" swaps them for
    // a list of another length, and Start keeps the height it had.
    property real restHeight: 0
    onImplicitHeightChanged: {
        if (!showAllRecents) {
            restHeight = implicitHeight;
        }
    }
    Component.onCompleted: restHeight = implicitHeight

    signal allAppsRequested()
    signal moreRequested()
    signal leaveUp()
    signal leaveDown()
    signal menuRequested(var model, int index, Item item, real x, real y, bool pinned)

    implicitHeight: column.implicitHeight

    readonly property var pins: launcher.rootModel.favoritesModel

    // Each pinned app's row in the pins model, read for its icon, name and
    // commands. The grid shows `tiles` instead: the pins in order, with the
    // apps of a folder gathered into one tile where the first of them is.
    Repeater {
        id: pinData
        model: home.pins
        delegate: Item {
            required property var model
        }
        onItemAdded: {
            home.pinRevision++;
            Qt.callLater(home.rebuild);
        }
        onItemRemoved: Qt.callLater(home.rebuild)
    }

    // Installing or removing any app makes Kicker send a bare layoutChanged,
    // and the Repeater remakes every row object. appData() reads this so a
    // tile drops the destroyed one: the rebuild that follows gives an equal
    // tile list, and GridView keeps its delegates for an equal model.
    property int pinRevision: 0

    Connections {
        target: home.pins
        function onRowsMoved() { Qt.callLater(home.rebuild); }
        function onModelReset() { Qt.callLater(home.rebuild); }
    }
    Connections {
        target: home.launcher
        function onPinFoldersChanged() { Qt.callLater(home.rebuild); }
    }

    property var tiles: []

    readonly property bool folderOpen: folderView.folder.length > 0

    function closeFolder(): void {
        folderView.close();
    }

    function appData(row: int): var {
        const item = row >= 0 && pinRevision >= 0 ? pinData.itemAt(row) : null;
        return item ? item.model : null;
    }

    function pinIds(): var {
        const ids = [];
        for (let i = 0; i < pinData.count; i++) {
            const data = appData(i);
            ids.push(data ? String(data.favoriteId) : "");
        }
        return ids;
    }

    function rebuild(): void {
        const ids = pinIds();
        // Apps unpinned elsewhere leave their folder.
        const folders = launcher.pinFolders
            .map(f => ({ id: f.id, name: f.name, apps: f.apps.filter(a => ids.includes(a)) }))
            .filter(f => f.apps.length > 1);
        if (JSON.stringify(folders) !== JSON.stringify(launcher.pinFolders)) {
            launcher.setPinFolders(folders);
            return;
        }
        const out = [];
        const placed = new Set();
        for (let row = 0; row < ids.length; row++) {
            const folder = folders.find(f => f.apps.includes(ids[row]));
            if (!folder) {
                out.push({ kind: "app", row: row, id: ids[row] });
            } else if (!placed.has(folder.id)) {
                placed.add(folder.id);
                out.push({ kind: "folder", folder: folder.id, name: folder.name, apps: folder.apps,
                    rows: folder.apps.map(a => ids.indexOf(a)) });
            }
        }
        tiles = out;
        if (folderView.folder.length > 0 && !folders.some(f => f.id === folderView.folder)) {
            folderView.close();
        }
    }

    // Puts the pins in this order, one move at a time, as the pins model
    // only moves single rows.
    function applyOrder(order: var): void {
        const ids = pinIds();
        for (let i = 0; i < order.length; i++) {
            const at = ids.indexOf(order[i]);
            if (at >= 0 && at !== i) {
                pins.moveRow(at, i);
                ids.splice(i, 0, ids.splice(at, 1)[0]);
            }
        }
    }

    // Qt's JavaScript has no Array.flatMap.
    function concatMap(list: var, fn: var): var {
        return list.reduce((all, item) => all.concat(fn(item)), []);
    }

    function flatten(list: var, folders: var): var {
        return concatMap(list, t => t.kind === "app" ? [t.id] : (folders.find(f => f.id === t.folder) || { apps: [] }).apps);
    }

    function moveTile(from: int, to: int): void {
        const list = tiles.slice();
        list.splice(to, 0, list.splice(from, 1)[0]);
        applyOrder(flatten(list, launcher.pinFolders));
    }

    // An app dropped on an app makes a folder where the second one was; an
    // app dropped on a folder joins it at the end. Folders do not go into
    // folders, as on Windows.
    function mergeTiles(from: int, onto: int): void {
        const moving = tiles[from];
        const target = tiles[onto];
        if (!moving || !target || moving.kind !== "app") {
            return;
        }
        const folders = launcher.pinFolders.map(f => ({ id: f.id, name: f.name, apps: f.apps.slice() }));
        let folderId = target.folder;
        if (target.kind === "folder") {
            const folder = folders.find(f => f.id === target.folder);
            if (!folder) {
                return;
            }
            folder.apps.push(moving.id);
        } else {
            folderId = "folder-" + Date.now();
            folders.push({ id: folderId, name: qsTr("Folder"), apps: [target.id, moving.id] });
        }
        // The folder's apps sit together among the pins, where the target was.
        const list = tiles.filter((t, i) => i !== from)
            .map(t => t === target ? { kind: "folder", folder: folderId } : t);
        launcher.setPinFolders(folders);
        applyOrder(flatten(list, folders));
    }

    // Out of a folder, the app lands just after it.
    function takeOut(folderId: string, appId: string): void {
        const folders = launcher.pinFolders.map(f => ({ id: f.id, name: f.name,
            apps: f.id === folderId ? f.apps.filter(a => a !== appId) : f.apps.slice() }));
        const order = concatMap(tiles, t => {
            if (t.kind === "app") {
                return t.id === appId ? [] : [t.id];
            }
            const apps = (folders.find(f => f.id === t.folder) || { apps: [] }).apps;
            return t.folder === folderId ? apps.concat(appId) : apps;
        });
        launcher.setPinFolders(folders);
        applyOrder(order);
    }

    function reorderFolder(folderId: string, from: int, to: int): void {
        launcher.setPinFolders(launcher.pinFolders.map(f => {
            if (f.id !== folderId) {
                return f;
            }
            const apps = f.apps.slice();
            apps.splice(to, 0, apps.splice(from, 1)[0]);
            return { id: f.id, name: f.name, apps: apps };
        }));
    }

    function renameFolder(folderId: string, name: string): void {
        const clean = name.trim();
        launcher.setPinFolders(launcher.pinFolders.map(f => f.id === folderId
            ? { id: f.id, name: clean.length > 0 ? clean : qsTr("Folder"), apps: f.apps } : f));
    }

    function folderTile(folderId: string): var {
        return tiles.find(t => t.kind === "folder" && t.folder === folderId) || null;
    }

    Column {
        id: column
        width: parent.width
        spacing: Style.sectionGap

        Column {
            width: parent.width
            spacing: 8
            visible: !home.showAllRecents

            SectionHeader {
                id: pinnedHeader
                width: parent.width
                title: qsTr("Pinned")
                buttonText: qsTr("All apps")
                onActivated: home.allAppsRequested()
            }

            Item {
                width: parent.width
                height: pinnedGrid.implicitHeight

                NavGrid {
                    id: pinnedGrid
                    anchors.fill: parent
                    anchors.rightMargin: -gap
                    columns: 6
                    // The design's 92 px holds a 42 px icon and one line of
                    // 12 px text; larger text makes the line taller.
                    rowHeight: 92 + UI.Style.px(12) - 12
                    model: home.tiles
                    delegate: AppTile {
                        grid: pinnedGrid
                        home: home
                    }
                    reorder: true
                    mergeable: true
                    canMerge: (from, onto) => home.tiles[from] !== undefined && home.tiles[from].kind === "app"
                    onMoved: (from, to) => home.moveTile(from, to)
                    onMerged: (from, onto) => home.mergeTiles(from, onto)
                    onActivated: index => {
                        const tile = home.tiles[index];
                        if (!tile) {
                            return;
                        }
                        if (tile.kind === "folder") {
                            folderView.open(tile.folder);
                        } else if (home.pins.trigger(tile.row, "", null)) {
                            home.launcher.close();
                        }
                    }
                    onMenuRequested: (index, item, x, y) => {
                        const tile = home.tiles[index];
                        if (tile && tile.kind === "app") {
                            home.menuRequested(home.pins, tile.row, item, x, y, true);
                        }
                    }
                    onExitUp: home.leaveUp()
                    onExitDown: home.hasRecents ? recentGrid.enter(false) : home.leaveDown()
                }

                FolderView {
                    id: folderView
                    home: home
                    // Over the pinned apps, as Windows opens a folder in place.
                    x: (parent.width - width) / 2
                    y: 0
                    z: 5
                    onClosed: pinnedGrid.forceActiveFocus()
                }

                UI.Label {
                    anchors.centerIn: parent
                    visible: pinnedGrid.count === 0
                    size: 12
                    color: UI.Style.inkMuted
                    text: qsTr("Pin apps from All apps to keep them here")
                }
            }
        }

        WindowsSetupSection {
            width: parent.width
            launcher: home.launcher
        }

        Column {
            width: parent.width
            spacing: 8
            visible: home.hasRecents

            SectionHeader {
                id: recentHeader
                width: parent.width
                title: home.showAllRecents ? qsTr("Recommended") : qsTr("Pick up where you left off")
                buttonText: home.showAllRecents ? qsTr("Back") : home.moreRecents ? qsTr("More") : ""
                backward: home.showAllRecents
                onActivated: home.moreRequested()
            }

            NavGrid {
                id: recentGrid
                width: parent.width + gap
                height: implicitHeight
                columns: 2
                limit: home.showAllRecents ? 0 : 6
                maxRows: home.showAllRecents ? 6 : 3
                rowHeight: 58 + UI.Style.px(13) + UI.Style.px(12) - 25
                model: home.launcher.recentModel
                delegate: RecentRow {
                    grid: recentGrid
                }
                onActivated: index => {
                    if (model.trigger(index, "", null)) {
                        home.launcher.close();
                    }
                }
                onMenuRequested: (index, item, x, y) => home.menuRequested(model, index, item, x, y, false)
                onExitUp: home.showAllRecents || pinnedGrid.count === 0 ? home.leaveUp() : pinnedGrid.enter(true)
                onExitDown: home.leaveDown()
            }
        }
    }
}
