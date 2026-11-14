import QtQuick
import QtQuick.Window
import WildOS.Store
import WildOS.UI as UI

// The Store window as the StoreDiscover board draws it: a 248px sidebar
// with the title, search, the sections and the privacy note, and the
// current page on the right under a 46px bar that holds the way back and
// the window buttons.
UI.AppWindow {
    id: root

    required property string startApp
    required property string startSearch
    required property string startPage

    property var current: ({ page: "", arg: "", section: "discover" })
    property var history: []
    property var pending: null
    // The board's sidebar, wider when larger text needs it.
    readonly property real navWidth: {
        let widest = 0;
        for (let i = 0; i < nav.children.length; ++i) {
            widest = Math.max(widest, nav.children[i].implicitWidth);
        }
        return Math.max(Metrics.navWidth, widest + 20);
    }

    // The board's 1384 by 802, smaller on a smaller screen.
    width: Screen.desktopAvailableWidth > 0 ? Math.max(minimumWidth, Math.min(1384, Screen.desktopAvailableWidth - 48)) : 1384
    height: Screen.desktopAvailableHeight > 0 ? Math.max(minimumHeight, Math.min(802, Screen.desktopAvailableHeight - 48)) : 802
    minimumWidth: 880
    minimumHeight: 560
    visible: true
    title: "Store"
    deep: true

    readonly property var sections: ["discover", "wildos", "swap", "create", "work", "play", "develop", "categories", "windowsapps", "updates", "library"]
    // Names other parts of Wild OS, the command line and D-Bus may still
    // use for the pages these replaced.
    readonly property var aliases: ({ home: "discover", installed: "library", windows: "swap" })

    function titleOf(entry) {
        switch (entry.page) {
        case "discover": return "Discover";
        case "wildos": return "Wild OS";
        case "swap": return "Swap from Windows";
        case "create": return "Create";
        case "work": return "Work";
        case "play": return "Play";
        case "develop": return "Develop";
        case "categories": return "Categories";
        case "apps": return entry.arg ? Catalog.categoryLabel(entry.arg) : "All apps";
        case "games": return entry.arg ? Catalog.categoryLabel(entry.arg) : "Games";
        case "windowsapps": return "Windows apps";
        case "updates": return "Updates";
        case "library": return "Library";
        case "search": return "Search";
        case "app": {
            const a = Catalog.app(entry.arg);
            return a ? a.name : "App";
        }
        case "file": return "Package file";
        }
        return "Discover";
    }

    function load(entry) {
        current = entry;
        if (entry.page !== "search") {
            search.text = "";
        }
        const urls = {
            discover: "pages/DiscoverPage.qml",
            wildos: "pages/WildOSPage.qml",
            swap: "pages/SwapPage.qml",
            create: "pages/CategoryPage.qml",
            work: "pages/CategoryPage.qml",
            play: "pages/CategoryPage.qml",
            develop: "pages/CategoryPage.qml",
            categories: "pages/CategoriesPage.qml",
            apps: "pages/BrowsePage.qml",
            games: "pages/BrowsePage.qml",
            windowsapps: "pages/WindowsAppsPage.qml",
            updates: "pages/UpdatesPage.qml",
            library: "pages/LibraryPage.qml",
            search: "pages/SearchPage.qml",
            app: "pages/AppPage.qml",
            file: "pages/FilePage.qml"
        };
        pageLoader.setSource(urls[entry.page] || urls.discover, { store: root, arg: entry.arg, kind: entry.page });
    }

    function open(page, arg) {
        page = aliases[page] || page;
        const isSection = sections.indexOf(page) >= 0;
        // A package file belongs to no section.
        const section = isSection ? page : (page === "apps" || page === "games") ? "categories" : page === "file" ? "" : current.section;
        const entry = { page: page, arg: arg || "", section: section };
        if (current.page === entry.page && current.arg === entry.arg) {
            return;
        }
        if (isSection) {
            // Picking a section starts a fresh trail.
            history = [];
        } else if (current.page.length > 0 && !(current.page === page && (page === "apps" || page === "games"))) {
            // Going deeper remembers the way back; another filter on the
            // same list does not.
            history = history.concat([current]);
        }
        load(entry);
    }

    function openApp(key) {
        open("app", key);
    }

    // Get on a Flathub app whose choices are known asks first
    // (StoreConsent.dc.html). True when the dialog took over.
    function requestInstall(app) {
        if (app && !app.installed && app.source === "flatpak" && app.consentItems.length > 0) {
            consent.ask(app);
            return true;
        }
        return false;
    }

    // The menu every app row, card and tile shares (AppMenuArea).
    function showAppMenu(key, item, x, y) {
        const a = Catalog.app(key);
        if (!a) {
            return;
        }
        const entries = [{ text: "See details", icon: "info", run: () => root.openApp(key) }];
        if (a.installed && a.canOpen) {
            entries.push({ text: "Open", icon: "open", run: () => a.open() });
        }
        if (a.installed && a.updateAvailable && !a.busy && !a.queued) {
            entries.push({ text: "Update", icon: "update", run: () => a.update() });
        }
        if (!a.installed) {
            entries.push({ text: "Install", icon: "download", enabled: a.source.length > 0 && !a.busy && !a.queued, run: () => {
                if (!root.requestInstall(a)) {
                    a.install();
                }
            } });
        }
        entries.push({ separator: true });
        entries.push({ text: "Copy link", icon: "link", enabled: a.appstreamId.length > 0, run: () => root.copyLink(a) });
        if (a.installed) {
            entries.push({ separator: true });
            entries.push({ text: "Uninstall", icon: "delete", danger: true, enabled: !a.busy && !a.queued, run: () => root.askRemove(a) });
        }
        appMenu.entries = entries;
        appMenu.popup(item, x, y);
    }

    // appstream: links open the app's page in the Store, here or on any
    // Linux desktop.
    function copyLink(app) {
        clipboard.text = "appstream://" + app.appstreamId;
        clipboard.selectAll();
        clipboard.copy();
        toast.note("Link copied");
    }

    property var removing: null
    function askRemove(app) {
        removing = app;
        removeDialog.open();
    }

    function back() {
        if (history.length === 0) {
            open("discover");
            return;
        }
        const previous = history[history.length - 1];
        history = history.slice(0, history.length - 1);
        load(previous);
    }

    function backTitle() {
        return history.length > 0 ? titleOf(history[history.length - 1]) : "Discover";
    }

    function runSearch(text) {
        const t = text.trim();
        if (t.length === 0) {
            if (current.page === "search") {
                back();
            }
            return;
        }
        if (current.page === "search") {
            current = { page: "search", arg: t, section: current.section };
            if (pageLoader.item) {
                pageLoader.item.arg = t;
            }
        } else {
            open("search", t);
        }
    }

    // From the command line, D-Bus and appstream: links. A package file
    // comes as page "file" with its path in app, and needs no catalogue.
    function openTarget(app, text, page) {
        if (page === "file") {
            open("file", app);
            return;
        }
        if (!Catalog.ready) {
            pending = { app: app, text: text, page: page };
            return;
        }
        if (app && app.length > 0) {
            const key = Catalog.resolve(app);
            if (key.length > 0) {
                openApp(key);
            } else {
                // Not in either catalogue: show what a search finds instead
                // of an empty page.
                search.text = app.replace(/^appstream:\/*/, "");
                runSearch(search.text);
            }
        } else if (text && text.length > 0) {
            search.text = text;
            runSearch(text);
        } else if (page && page.length > 0) {
            open(page);
        }
    }

    Component.onCompleted: {
        if (startPage === "file") {
            open("discover");
            open("file", startApp);
            return;
        }
        open(startPage);
        if (startApp.length > 0 || startSearch.length > 0) {
            openTarget(startApp, startSearch, "");
        }
    }

    Connections {
        target: Catalog
        function onReadyChanged() {
            if (Catalog.ready && root.pending) {
                const p = root.pending;
                root.pending = null;
                root.openTarget(p.app, p.text, p.page);
            }
        }
        function onInstalled(key) {
            toast.showFor(key, "installed");
        }
        function onFailed(key, message) {
            toast.showFor(key, "failed", message);
        }
    }

    Shortcut {
        sequences: [StandardKey.Back, "Alt+Left"]
        onActivated: root.back()
    }

    Shortcut {
        sequences: [StandardKey.Find, "Ctrl+E"]
        onActivated: search.forceActiveFocus()
    }

    // The window's 1px edge sits outside the content.
    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: root.maximised ? 0 : 1

        Rectangle {
            id: sidebar
            width: root.navWidth
            height: parent.height
            topLeftRadius: root.maximised ? 0 : UI.Style.windowRadius - 1
            bottomLeftRadius: topLeftRadius
            color: Metrics.sidebar

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Metrics.divider
            }
        }

        UI.TitleBar {
            id: titleBar
            width: parent.width
            height: Metrics.topBarHeight
            frame: root.frame
            buttons.buttonHeight: Metrics.topBarHeight

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "Store"
                size: 14
                font.weight: UI.Style.weightSemiBold
                color: root.active ? UI.Style.ink : UI.Style.inkMuted
            }

            StoreLink {
                // The page's 24px in from the sidebar, less the title bar's
                // own 16px and the link's 4px of padding.
                x: root.navWidth + 24 - 16 - 4
                anchors.verticalCenter: parent.verticalCenter
                visible: root.history.length > 0
                iconName: "chevron-left"
                text: root.backTitle()
                Accessible.name: "Back to " + root.backTitle()
                onClicked: root.back()
            }
        }

        UI.SearchField {
            id: search
            x: 16
            y: Metrics.topBarHeight + 2
            width: root.navWidth - 32
            height: 34
            iconLeading: true
            iconInset: 11
            iconSize: 15
            font.pixelSize: UI.Style.px(13.5)
            placeholderText: "Search"
            placeholderTextColor: Metrics.inkTertiary
            Accessible.name: "Search apps"
            Accessible.description: "Search apps, or the Windows app you used before"
            background: Rectangle {
                radius: 9
                color: search.activeFocus ? Metrics.buttonFill : UI.Style.tone(0.06)
                border.width: search.activeFocus ? 2 : 0
                border.color: UI.Style.accent
            }
            onTextEdited: searchDelay.restart()
            // The clear button and Escape empty the box without an edit.
            onTextChanged: if (text.length === 0) searchDelay.restart()
            onAccepted: {
                searchDelay.stop();
                root.runSearch(text);
            }
        }

        Timer {
            id: searchDelay
            interval: 250
            onTriggered: root.runSearch(search.text)
        }

        Column {
            id: nav
            x: 10
            y: search.y + search.height + 14
            width: root.navWidth - 20
            spacing: 2

            NavItem {
                iconName: "discover"
                text: "Discover"
                current: root.current.section === "discover"
                onClicked: root.open("discover")
            }

            NavItem {
                mark: true
                text: "Wild OS"
                current: root.current.section === "wildos"
                onClicked: root.open("wildos")
            }

            NavItem {
                iconName: "swap-windows"
                text: "Swap from Windows"
                current: root.current.section === "swap"
                onClicked: root.open("swap")
            }

            NavItem {
                iconName: "create"
                text: "Create"
                current: root.current.section === "create"
                onClicked: root.open("create")
            }

            NavItem {
                iconName: "work"
                text: "Work"
                current: root.current.section === "work"
                onClicked: root.open("work")
            }

            NavItem {
                iconName: "play"
                text: "Play"
                current: root.current.section === "play"
                onClicked: root.open("play")
            }

            NavItem {
                iconName: "develop"
                text: "Develop"
                current: root.current.section === "develop"
                onClicked: root.open("develop")
            }

            NavItem {
                iconName: "categories"
                text: "Categories"
                current: root.current.section === "categories"
                onClicked: root.open("categories")
            }

            NavItem {
                iconName: "windows-apps"
                text: "Windows apps"
                current: root.current.section === "windowsapps"
                onClicked: root.open("windowsapps")
            }

            Item {
                width: parent.width
                height: 25

                Rectangle {
                    x: 10
                    y: 12
                    width: parent.width - 20
                    height: 1
                    color: Metrics.divider
                }
            }

            NavItem {
                iconName: "updates"
                text: "Updates"
                badge: Catalog.updateCount
                current: root.current.section === "updates"
                onClicked: root.open("updates")
            }

            NavItem {
                iconName: "library"
                text: "Library"
                working: Catalog.workingCount > 0
                current: root.current.section === "library"
                onClicked: root.open("library")
            }
        }

        // What the Store knows about the person using it, which is nothing
        // beyond what any download gives away.
        Rectangle {
            id: privacy
            x: 14
            width: root.navWidth - 28
            height: privacyRow.implicitHeight + 26
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            // Larger text on a short window would push it over the
            // sections; it steps aside rather than cover them.
            visible: y > nav.y + nav.height + 14
            radius: 12
            color: Metrics.privacyCard
            border.width: 1
            border.color: Metrics.divider

            Row {
                id: privacyRow
                x: 14
                y: 13
                width: parent.width - 28
                spacing: 10

                StoreIcon {
                    name: "private"
                    size: 18
                    color: UI.Style.accent
                }

                Column {
                    width: parent.width - 28
                    spacing: 3

                    UI.Label {
                        width: parent.width
                        text: "No account needed"
                        font.weight: UI.Style.weightSemiBold
                    }

                    UI.Label {
                        width: parent.width
                        text: "The Store never asks who you are. Flathub sees your IP address when you download, like any website."
                        size: 12
                        lineHeight: 1.45
                        color: Metrics.inkSecondary
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                }
            }
        }

        Loader {
            id: pageLoader
            x: root.navWidth
            y: Metrics.topBarHeight
            width: parent.width - root.navWidth
            height: parent.height - Metrics.topBarHeight
            clip: true
            focus: true
            // Each page rises a little as it fades in.
            transform: UI.Reveal {
                item: pageLoader
                key: pageLoader.item
                dy: UI.Style.travelMedium
            }
        }

        Toast {
            id: toast
            anchors.horizontalCenter: pageLoader.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            onOpenRequested: key => root.openApp(key)
        }
    }

    UI.Menu {
        id: appMenu
    }

    ConsentDialog {
        id: consent
    }

    UI.Dialog {
        id: removeDialog
        title: root.removing ? "Uninstall " + root.removing.name + "?" : ""
        text: root.removing && root.removing.source === "flatpak"
            ? "Your files stay where they are. Its settings stay too, in case you install it again."
            : "Your files stay where they are. Wild OS will ask for the administrator password."
        buttons: [
            UI.Button {
                text: "Cancel"
                onClicked: removeDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: "Uninstall"
                onClicked: {
                    removeDialog.close();
                    root.removing.remove();
                }
            }
        ]
    }

    // QML has no clipboard of its own; a hidden text field copies for it.
    TextInput {
        id: clipboard
        visible: false
    }
}
