pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import "code/helpers.js" as Helpers
import WildOS.UI as UI

// Results for the current query: filter chips, a sectioned list with the best
// match first, and a details pane for whatever is selected.
FocusScope {
    id: search

    required property var launcher

    property string filter: "all"
    property var rows: []
    property int selected: -1
    property bool finished: false
    property int favouritesRevision: 0

    readonly property var source: launcher.runnerModel.count > 0 ? launcher.runnerModel.modelForRow(0) : null
    readonly property var current: selected >= 0 && selected < rows.length ? rows[selected] : null

    property alias chips: chips
    property alias resultList: list
    property alias openButton: openButton
    readonly property var actionButtons: {
        const buttons = [];
        for (let i = 0; i < actionRepeater.count; i++) {
            buttons.push(actionRepeater.itemAt(i));
        }
        return buttons;
    }

    signal leaveUp()
    signal menuRequested(var entries, Item item, real x, real y)

    readonly property var filters: [
        { key: "all", label: "All" },
        { key: "apps", label: "Apps" },
        { key: "files", label: "Files" },
        { key: "settings", label: "Settings" },
        { key: "folders", label: "Folders" },
    ]

    readonly property var sectionTitles: ({
        calc: "Calculator",
        apps: "Apps",
        files: "Files and folders",
        folders: "Files and folders",
        settings: "Settings",
    })

    readonly property var hiddenActions: ["addToDesktop", "addToPanel", "editApplication", "_kicker_fileItem_properties", "forget", "forgetAll", "_kicker_recentDocument", "_kicker_forgetRecentDocuments", "addToTaskManager"]

    function reset(): void {
        filter = "all";
        selected = -1;
        rows = [];
    }

    function categorize(m: var): string {
        const group = String(m.group ?? "");
        const url = String(m.url ?? "");
        const favoriteId = String(m.favoriteId ?? "");
        if (group === "Applications" || favoriteId.startsWith("applications:")) {
            // With Wild OS Settings installed, KDE's System Settings would be
            // a confusing second "Settings".
            if (launcher.hasWildSettings && Helpers.storageId(favoriteId) === "systemsettings") {
                return "hidden";
            }
            return "apps";
        }
        if (group === "System Settings" || group === "Settings") {
            return Helpers.settingsPage(url) ? "settings" : "hidden";
        }
        if (group === "Places" || group === "Folders" || url.endsWith("/")) {
            return "folders";
        }
        return "files";
    }

    function matchesFilter(kind: string): bool {
        switch (filter) {
        case "apps":
            return kind === "apps";
        case "files":
            return kind === "files" || kind === "folders";
        case "settings":
            return kind === "settings";
        case "folders":
            return kind === "folders";
        default:
            return kind !== "hidden";
        }
    }

    function describe(m: var, kind: string): var {
        const url = String(m.url ?? "");
        switch (kind) {
        case "apps":
            return {
                title: Helpers.appName(m.favoriteId, m.display),
                subtitle: "App",
                detail: "App",
            };
        case "settings": {
            const page = Helpers.settingsPage(url);
            return {
                title: page.title,
                subtitle: page.category,
                detail: page.category,
            };
        }
        case "folders": {
            const trail = Helpers.folderTrail(url, launcher.homePath);
            return {
                title: m.display ?? "",
                subtitle: trail.length > 0 ? trail : "Folder",
                detail: trail.length > 0 ? "Folder in " + trail : "Folder",
            };
        }
        default: {
            const trail = Helpers.folderTrail(url, launcher.homePath);
            return {
                title: Helpers.withoutExtension(m.display),
                subtitle: trail.length > 0 ? trail : (m.description ?? ""),
                detail: trail.length > 0 ? "File in " + trail : (m.description ?? "File"),
            };
        }
        }
    }

    function rebuild(): void {
        const items = [];
        const calc = filter === "all" ? Helpers.calculate(launcher.query) : null;
        if (calc) {
            items.push({ header: false, kind: "calc", src: -1, title: calc.value, subtitle: calc.expression, detail: calc.expression + " =", icon: "accessories-calculator", plain: calc.plain });
        }
        // Wild OS's own Settings pages, which KRunner doesn't know: first
        // when the name was typed, after the rest when only a keyword was.
        const pages = (filter === "all" || filter === "settings" ? Helpers.wildosPageMatches(launcher.query) : [])
            .map(p => ({ header: false, kind: "settings", src: -1, page: p.page, title: p.title, subtitle: p.category, detail: p.category,
                         icon: "preferences-system", plain: "", named: p.named }));
        for (const p of pages.filter(p => p.named)) {
            items.push(p);
        }
        for (let i = 0; i < matches.count; i++) {
            const entry = matches.objectAt(i);
            if (!entry) {
                continue;
            }
            const kind = categorize(entry.model);
            if (!matchesFilter(kind)) {
                continue;
            }
            const text = describe(entry.model, kind);
            // The icon is copied now, not read from the match later: the
            // runner model resets as results arrive, which replaces every
            // match object, and a row still holding the old one would fall
            // back to the generic app icon.
            const icon = entry.model.decoration || (kind === "folders" ? "folder" : "unknown");
            items.push({ header: false, kind: kind, src: i, title: text.title, subtitle: text.subtitle, detail: text.detail, icon: icon, plain: "" });
        }
        for (const p of pages.filter(p => !p.named)) {
            items.push(p);
        }

        const out = [];
        if (items.length > 0) {
            out.push({ header: true, title: "Best match", first: true });
            out.push(Object.assign({ best: true }, items[0]));
            const order = [];
            const groups = {};
            for (let i = 1; i < items.length; i++) {
                const key = sectionTitles[items[i].kind];
                if (!groups[key]) {
                    groups[key] = [];
                    order.push(key);
                }
                groups[key].push(Object.assign({ best: false }, items[i]));
            }
            for (const key of order) {
                out.push({ header: true, title: key, first: false });
                for (const item of groups[key]) {
                    out.push(item);
                }
            }
        }

        const previous = current;
        rows = out;
        let keep = -1;
        if (previous) {
            keep = out.findIndex(r => !r.header && r.src === previous.src && r.kind === previous.kind && r.title === previous.title);
        }
        selected = keep >= 0 ? keep : (out.length > 1 ? 1 : -1);
    }

    function moveSelection(step: int): void {
        let index = selected;
        do {
            index += step;
        } while (index >= 0 && index < rows.length && rows[index].header);
        if (index >= 0 && index < rows.length) {
            selected = index;
            list.positionViewAtIndex(index, ListView.Contain);
            if (index === 1) {
                list.positionViewAtBeginning();
            }
        }
    }

    function modelOf(entry: var): var {
        return entry && entry.src >= 0 ? matches.objectAt(entry.src) : null;
    }

    function open(entry: var): void {
        if (!entry) {
            return;
        }
        if (entry.kind === "calc") {
            clipboard.text = entry.plain;
            clipboard.selectAll();
            clipboard.copy();
            launcher.close();
            return;
        }
        if (entry.kind === "settings") {
            const page = entry.page ? { page: entry.page } : Helpers.settingsPage(modelOf(entry).model.url);
            if (!page || !launcher.openSettingsPage(page.page)) {
                launcher.settingsMissing();
            }
            return;
        }
        source.trigger(entry.src, "", null);
        launcher.close();
    }

    // Commands for a result, worded as on Windows: Start, taskbar and Store
    // commands for apps, "Open file location" for files, then a few of the
    // app's own shortcuts if there is room.
    function actionsFor(entry: var, revision: int, limit: int): var {
        const match = modelOf(entry);
        if (!match) {
            return [];
        }
        const m = match.model;
        const actions = m.actionList ? Array.from(m.actionList) : [];
        let result = [];

        if (entry.kind === "apps") {
            result = launcher.appCommands(String(m.favoriteId ?? ""), actions, (actionId, argument) => search.source.trigger(entry.src, actionId, argument), -1)
                .filter(command => !command.separator)
                .map(command => ({ text: command.text, icon: command.icon, run: () => {
                    command.run();
                    search.favouritesRevision++;
                } }));
        }
        // KDE's System Settings lists its own modules as shortcuts, which
        // would put KDE names back into results.
        const ownActions = Helpers.storageId(m.favoriteId) === "systemsettings" ? [] : actions;
        for (const action of ownActions) {
            if (action.type === "separator" || action.type === "title" || hiddenActions.includes(action.actionId) || !action.text) {
                continue;
            }
            // Installing and removing add-ons is the Store's job, and these
            // raw desktop actions read as clutter next to Open.
            if (/add-?ons?|uninstall/i.test(action.text)) {
                continue;
            }
            result.push({
                text: Helpers.actionLabel(action),
                icon: action.actionId === "openParentFolder" ? "open-folder" : "external",
                run: () => {
                    if (search.source.trigger(entry.src, action.actionId, action.actionArgument)) {
                        search.launcher.close();
                    }
                },
            });
        }
        return result.slice(0, limit);
    }

    function showMenuFor(entry: var, item: Item, x: real, y: real): void {
        const open = { text: entry.kind === "calc" ? "Copy result" : "Open", icon: "external", run: () => search.open(entry) };
        search.menuRequested([open].concat(actionsFor(entry, favouritesRevision, 6)), item, x, y);
    }

    // The design shows a handful of clear actions under Open, not an app's
    // whole jump list. Start's own actions come first, so they survive the cut.
    readonly property int maxActions: 3

    Instantiator {
        id: matches
        model: search.source
        delegate: QtObject {
            required property var model
            required property int index
        }
        onObjectAdded: rebuildTimer.restart()
        onObjectRemoved: rebuildTimer.restart()
    }

    // Runners report in bursts; batching keeps the list from flickering.
    Timer {
        id: rebuildTimer
        interval: 30
        onTriggered: search.rebuild()
    }

    Connections {
        target: search.launcher
        function onQueryChanged() {
            search.finished = false;
            rebuildTimer.restart();
        }
    }

    Connections {
        target: search.launcher.runnerModel
        function onQueryFinished() {
            search.finished = true;
            rebuildTimer.restart();
        }
        function onCountChanged() {
            rebuildTimer.restart();
        }
    }

    onFilterChanged: rebuild()

    TextEdit {
        id: clipboard
        visible: false
    }

    Row {
        id: chips
        spacing: 6
        height: 30

        property int focusedChip: 0

        Accessible.role: Accessible.PageTabList

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Left) {
                focusedChip = Math.max(0, focusedChip - 1);
            } else if (event.key === Qt.Key_Right) {
                focusedChip = Math.min(search.filters.length - 1, focusedChip + 1);
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                search.filter = search.filters[focusedChip].key;
            } else if (event.key === Qt.Key_Down) {
                list.forceActiveFocus(Qt.TabFocusReason);
            } else if (event.key === Qt.Key_Up) {
                search.leaveUp();
            } else {
                return;
            }
            Nav.keyboard = true;
            event.accepted = true;
        }

        onActiveFocusChanged: {
            if (activeFocus) {
                focusedChip = search.filters.findIndex(f => f.key === search.filter);
            }
        }

        Repeater {
            model: search.filters

            delegate: Item {
                id: chip

                required property var modelData
                required property int index
                readonly property bool active: search.filter === modelData.key

                width: label.implicitWidth + 28
                height: 30

                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.checked: active

                Rectangle {
                    anchors.fill: parent
                    radius: 15
                    color: chip.active ? UI.Style.accent : chipMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceTile
                    Behavior on color {
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }
                }

                UI.Label {
                    id: label
                    anchors.centerIn: parent
                    size: 12
                    font.weight: chip.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: chip.active ? UI.Style.textOnAccent : UI.Style.ink
                    text: chip.modelData.label
                }

                MouseArea {
                    id: chipMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: search.filter = chip.modelData.key
                }

                UI.FocusRing {
                    targetRadius: 15
                    visible: chips.activeFocus && chips.focusedChip === chip.index && Nav.keyboard
                }
            }
        }
    }

    Item {
        id: body
        anchors.top: chips.bottom
        anchors.topMargin: 18
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        ListView {
            id: list
            width: 360
            height: parent.height
            visible: search.rows.length > 0
            clip: true
            spacing: 4
            model: search.rows
            currentIndex: search.selected
            highlightFollowsCurrentItem: false
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds

            T.ScrollBar.vertical: UI.ScrollBar {}

            Keys.onPressed: event => {
                switch (event.key) {
                case Qt.Key_Up:
                    if (search.selected <= 1) {
                        search.leaveUp();
                    } else {
                        search.moveSelection(-1);
                    }
                    break;
                case Qt.Key_Down:
                    search.moveSelection(1);
                    break;
                case Qt.Key_Right:
                    openButton.forceActiveFocus(Qt.TabFocusReason);
                    break;
                case Qt.Key_Return:
                case Qt.Key_Enter:
                    search.open(search.current);
                    break;
                default:
                    return;
                }
                Nav.keyboard = true;
                event.accepted = true;
            }

            delegate: Item {
                id: entry

                required property var modelData
                required property int index

                readonly property bool isHeader: modelData.header
                readonly property bool isBest: modelData.best === true
                readonly property bool isSelected: search.selected === index

                width: ListView.view.width
                height: isHeader ? (modelData.first ? 18 : 28) : (isBest ? 64 : 50)

                Accessible.role: isHeader ? Accessible.Heading : Accessible.ListItem
                Accessible.name: modelData.title

                UI.Label {
                    visible: entry.isHeader
                    x: 8
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    height: 16
                    size: 12
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkMuted
                    text: entry.modelData.title
                }

                Rectangle {
                    visible: !entry.isHeader
                    anchors.fill: parent
                    radius: Style.radiusItem
                    color: entry.isSelected ? UI.Style.accentSelected : rowMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                    Behavior on color {
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }
                }

                // One box per row kind: the best match larger, every other
                // result the same 26 px whatever kind it is.
                AppIcon {
                    id: rowIcon
                    visible: !entry.isHeader
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: entry.isBest ? 36 : 26
                    height: width
                    source: entry.modelData.icon ?? ""
                }

                Column {
                    visible: !entry.isHeader
                    anchors.left: parent.left
                    anchors.leftMargin: entry.isBest ? 60 : 50
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: entry.isBest ? 2 : 1

                    UI.Label {
                        width: parent.width
                        size: entry.isBest ? 14 : 13
                        font.weight: entry.isBest ? UI.Style.weightSemiBold : UI.Style.weightMedium
                        color: entry.isSelected ? UI.Style.inkStrong : UI.Style.ink
                        text: entry.modelData.title
                    }

                    UI.Label {
                        width: parent.width
                        visible: text.length > 0
                        size: 12
                        color: entry.isBest ? UI.Style.inkStatus : UI.Style.inkMuted
                        text: entry.modelData.subtitle ?? ""
                    }
                }

                MouseArea {
                    id: rowMouse
                    visible: !entry.isHeader
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: event => {
                        search.selected = entry.index;
                        if (event.button === Qt.LeftButton) {
                            search.open(entry.modelData);
                        } else {
                            search.showMenuFor(entry.modelData, entry, event.x, event.y);
                        }
                    }
                }

                UI.FocusRing {
                    anchors.margins: -2
                    radius: Style.radiusItem + 2
                    visible: entry.isSelected && list.activeFocus && Nav.keyboard
                }
            }
        }

        Rectangle {
            id: details
            visible: search.current !== null
            anchors.left: list.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            height: parent.height
            radius: 16
            color: UI.Style.surfaceRaised
            border.width: 1
            border.color: UI.Style.borderSubtle
            clip: true

            Column {
                id: detailsColumn
                x: 22
                y: 26
                width: parent.width - 44
                spacing: 6

                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 72
                    height: 72
                    source: search.current ? search.current.icon : ""
                }

                UI.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, parent.width)
                    topPadding: 8
                    horizontalAlignment: Text.AlignHCenter
                    size: 17
                    font.weight: UI.Style.weightSemiBold
                    text: search.current ? search.current.title : ""
                }

                UI.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, parent.width)
                    bottomPadding: 14
                    horizontalAlignment: Text.AlignHCenter
                    size: 12
                    color: UI.Style.inkMuted
                    text: search.current ? search.current.detail : ""
                }

                UI.Button {
                    id: openButton
                    width: parent.width
                    kind: "primary"
                    size: "large"
                    focusPolicy: Qt.NoFocus
                    text: search.current && search.current.kind === "calc" ? "Copy result" : "Open"
                    onClicked: search.open(search.current)

                    Keys.onLeftPressed: list.forceActiveFocus(Qt.BacktabFocusReason)
                    Keys.onDownPressed: {
                        if (actionRepeater.count > 0) {
                            actionRepeater.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
                        }
                    }
                }

                Column {
                    id: actionsColumn
                    width: parent.width
                    topPadding: 8
                    spacing: 2

                    // Only whole rows that fit inside the card, so a short window
                    // never pushes actions over the strip below.
                    readonly property int fittingRows: Math.max(0, Math.floor((details.height - 26 - detailsColumn.y - y - topPadding + spacing) / 40))

                    Repeater {
                        id: actionRepeater
                        model: search.actionsFor(search.current, search.favouritesRevision, Math.min(search.maxActions, actionsColumn.fittingRows))

                        delegate: WButton {
                            id: action

                            required property var modelData
                            required property int index

                            width: parent.width
                            height: 38
                            radius: 8
                            accessibleName: modelData.text
                            onClicked: modelData.run()

                            Keys.onUpPressed: {
                                if (index > 0) {
                                    actionRepeater.itemAt(index - 1).forceActiveFocus(Qt.BacktabFocusReason);
                                } else {
                                    openButton.forceActiveFocus(Qt.BacktabFocusReason);
                                }
                            }
                            Keys.onDownPressed: {
                                if (index < actionRepeater.count - 1) {
                                    actionRepeater.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                            Keys.onLeftPressed: list.forceActiveFocus(Qt.BacktabFocusReason)

                            UI.Icon {
                                id: actionIcon
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                name: action.modelData.icon
                                size: 16
                                strokeWidth: 1.8
                                visible: name.length > 0
                            }

                            UI.Label {
                                x: 36
                                width: parent.width - 46
                                anchors.verticalCenter: parent.verticalCenter
                                text: action.modelData.text
                            }
                        }
                    }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -30
            spacing: 6
            visible: search.rows.length === 0 && search.finished && search.launcher.query.length > 0

            UI.Label {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, body.width - 40)
                horizontalAlignment: Text.AlignHCenter
                size: 15
                font.weight: UI.Style.weightSemiBold
                text: "No results for ‘" + search.launcher.query + "’"
            }

            UI.Label {
                anchors.horizontalCenter: parent.horizontalCenter
                size: 12
                color: UI.Style.inkMuted
                text: search.filter === "all" ? "Check the spelling or try a shorter word." : "Try All to search everything."
            }
        }
    }
}
