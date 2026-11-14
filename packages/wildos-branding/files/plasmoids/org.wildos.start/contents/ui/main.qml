pragma ComponentBehavior: Bound

import QtQuick
import QtCore
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.private.kicker as Kicker
import org.kde.plasma.private.sessions as Sessions
import org.kde.coreaddons as KCoreAddons
import org.kde.plasma.plasma5support as P5Support
import org.wildos.shell as WildOS
import "code/helpers.js" as Helpers

PlasmoidItem {
    id: start

    property string query: ""

    // Set before opening to start at All apps rather than the pinned apps.
    property bool openAtAllApps: false

    // KWin's hot corners and touchpad gestures ask for the app list through
    // plasmashell. Each screen's taskbar may have a Start; only the one on
    // the screen they name answers.
    Connections {
        target: WildOS.ShellRequests
        function onAllAppsRequested(x: int, y: int): void {
            const screen = Plasmoid.containment ? Plasmoid.containment.screenGeometry : null;
            if (!screen || x < screen.x || y < screen.y || x >= screen.x + screen.width || y >= screen.y + screen.height) {
                return;
            }
            if (start.expanded) {
                start.expanded = false;
            } else {
                start.openAtAllApps = true;
                start.expanded = true;
            }
        }
    }

    // Set by the Start button: an item in the panel whose centre is the centre
    // of the screen. The popup is anchored to it so Start opens centred.
    property Item popupAnchor: null

    readonly property string homeUrl: StandardPaths.writableLocation(StandardPaths.HomeLocation).toString()
    readonly property string homePath: homeUrl.replace(/^file:\/\//, "")

    readonly property Kicker.RootModel rootModel: Kicker.RootModel {
        id: kickerRoot

        autoPopulate: false
        appletInterface: start

        appNameFormat: 0
        flat: true
        sorted: true
        showSeparators: false
        showTopLevelItems: false
        showAllApps: true
        showAllAppsCategorized: false
        showRecentApps: false
        showRecentDocs: false
        showPowerSession: false
        showFavoritesPlaceholder: false

        onRefreshed: start.appsModel = kickerRoot.modelForRow(0)

        Component.onCompleted: {
            kickerRoot.favoritesModel.initForClient("org.wildos.start.favorites.instance-" + Plasmoid.id);
            // The default pins are copied once. After that the activity
            // manager owns them, so unpinning a default sticks.
            const defaults = Array.from(Plasmoid.configuration.favorites);
            if (!Plasmoid.configuration.favoritesPortedToKAstats) {
                if (kickerRoot.favoritesModel.count < 1) {
                    kickerRoot.favoritesModel.portOldFavorites(defaults);
                }
                Plasmoid.configuration.favoritesPortedToKAstats = true;
            } else {
                // Defaults added since this account's pins were made are
                // pinned once. Accounts from before the list was kept had
                // every pin in Helpers.pinsBeforeOffice.
                let offered = Array.from(Plasmoid.configuration.pinsOffered);
                if (offered.length === 0) {
                    offered = Helpers.pinsBeforeOffice;
                }
                for (const id of defaults) {
                    if (!offered.includes(id) && !Helpers.newAccountPins.includes(id) && !kickerRoot.favoritesModel.isFavorite(id)) {
                        const old = Helpers.replacesPin[id];
                        const pins = Array.from(kickerRoot.favoritesModel.favorites ?? []);
                        let place = old ? pins.indexOf(old) : -1;
                        if (old && place < 0) {
                            place = pins.indexOf("applications:" + old);
                        }
                        if (place >= 0) {
                            kickerRoot.favoritesModel.removeFavorite(old);
                            kickerRoot.favoritesModel.addFavorite(id, place);
                        } else {
                            kickerRoot.favoritesModel.addFavorite(id);
                        }
                    }
                }
            }
            Plasmoid.configuration.pinsOffered = defaults;
            kickerRoot.refresh();
        }
    }

    // Row 0 of the root model is the flat, sorted list of every app because
    // categories and the favourites placeholder are switched off above.
    property var appsModel: null

    // Search uses only runners that read this computer: installed apps,
    // System Settings pages, the Baloo file index, recent documents and
    // places. Web shortcuts, the calculator (which can download exchange
    // rates) and every other runner are left out on purpose.
    readonly property Kicker.RunnerModel runnerModel: Kicker.RunnerModel {
        appletInterface: start
        favoritesModel: start.rootModel.favoritesModel
        mergeResults: true
        runners: ["krunner_services", "krunner_systemsettings", "baloosearch", "krunner_recentdocuments", "krunner_placesrunner"]
        query: start.query
        onRequestUpdateQuery: newQuery => start.query = newQuery
    }

    readonly property Kicker.RecentUsageModel recentModel: Kicker.RecentUsageModel {
        shownItems: Kicker.RecentUsageModel.OnlyDocs
        favoritesModel: start.rootModel.favoritesModel
    }

    readonly property Sessions.SessionManagement session: Sessions.SessionManagement {}

    readonly property KCoreAddons.KUser user: KCoreAddons.KUser {}

    readonly property Instantiator appIndex: Instantiator {
        model: start.appsModel
        delegate: QtObject {
            required property var model
            readonly property string favoriteId: model.favoriteId ?? ""
            readonly property string url: String(model.url ?? "")
        }
        onObjectAdded: start.appsRevision++
        onObjectRemoved: start.appsRevision++
    }

    // Bumped whenever the installed apps change, so bindings that ask "is
    // this app installed" re-evaluate.
    property int appsRevision: 0
    readonly property bool hasWildSettings: appsRevision >= 0 && appRow("org.wildos.settings.desktop") >= 0
    readonly property bool hasStore: appsRevision >= 0 && (appRow("org.wildos.store.desktop") >= 0 || appRow("org.kde.discover.desktop") >= 0)

    readonly property P5Support.DataSource executable: P5Support.DataSource {
        engine: "executable"
        onNewData: sourceName => disconnectSource(sourceName)
    }

    function close(): void {
        start.expanded = false;
    }

    function appRow(desktopId: string): int {
        for (let i = 0; i < start.appIndex.count; i++) {
            const entry = start.appIndex.objectAt(i);
            if (entry && entry.favoriteId === desktopId) {
                return i;
            }
        }
        return -1;
    }

    // Launches an installed app by desktop file id through Kicker, so it is
    // started the same way as from the app list, with usage stats recorded.
    function launchApp(desktopId: string, fallbackUrl: string): void {
        const row = appRow(desktopId);
        if (row >= 0) {
            start.appsModel.trigger(row, "", null);
        } else if (fallbackUrl.length > 0) {
            Qt.openUrlExternally(fallbackUrl);
        }
        start.close();
    }

    function openSettings(): void {
        if (hasWildSettings) {
            launchApp("org.wildos.settings.desktop", "");
        } else {
            settingsMissing();
        }
    }

    // Settings ships with Wild OS; if it has gone, say so rather than open
    // KDE's own settings, which Wild OS never shows.
    function settingsMissing(): void {
        executable.connectSource("notify-send -a 'Wild OS' -i preferences-system 'Settings is missing' "
            + "'Reinstall it from the Store to change this.'");
        start.close();
    }

    // Opens a page of the Wild OS Settings app. Kicker cannot pass arguments,
    // so the app's own Exec line is run with --page through kstart, which
    // keeps it attributed to the app. Returns false when the app is missing
    // so callers can say so.
    function openSettingsPage(page: string): bool {
        const row = appRow("org.wildos.settings.desktop");
        if (row < 0) {
            return false;
        }
        const path = Helpers.localPath(start.appIndex.objectAt(row).url);
        if (page.length === 0 || path.length === 0) {
            start.appsModel.trigger(row, "", null);
        } else {
            const script = 'k=; command -v kstart >/dev/null 2>&1 && k="kstart --desktopfile org.wildos.settings --"; '
                + 'exec $k $(sed -n "s/^Exec=//p" "$0" | head -n 1 | sed "s/ %[a-zA-Z]//g") --page "$1"';
            executable.connectSource("sh -c " + Helpers.shellQuote(script) + " " + Helpers.shellQuote(path) + " " + Helpers.shellQuote(page));
        }
        start.close();
        return true;
    }

    function openAccountSettings(): void {
        if (!openSettingsPage("accounts")) {
            settingsMissing();
        }
    }

    function canUninstall(favoriteId: string): bool {
        if (favoriteId.length === 0 || Helpers.isCoreApp(favoriteId)) {
            return false;
        }
        return isHomeApp(favoriteId) || isWindowsApp(favoriteId) || Helpers.isOwnApp(favoriteId) || hasStore;
    }

    // A Windows app in Windows mode: Windows uninstalls it, through
    // Settings > Installed apps.
    function isWindowsApp(favoriteId: string): bool {
        return Helpers.storageId(favoriteId).indexOf("wildos-winmode-") === 0;
    }

    // An app that installed itself in the home folder (its own install
    // script, an AppImage). The Store has never heard of it, so Settings
    // removes it. Windows mode's launchers live there too; isWindowsApp
    // covers them.
    function isHomeApp(favoriteId: string): bool {
        const row = appRow(favoriteId);
        if (row < 0) {
            return false;
        }
        const url = String(start.appIndex.objectAt(row).url);
        return url.indexOf("/.local/share/applications/") >= 0 && Helpers.storageId(favoriteId).indexOf("wildos-") !== 0;
    }

    // Removing software is the Store's job, as on Windows, so Uninstall
    // opens the app's page there; Wild OS's own apps and apps in the home
    // folder or in Windows mode open their Uninstall in Settings > Installed
    // apps.
    function uninstallApp(favoriteId: string): void {
        if (isHomeApp(favoriteId) || isWindowsApp(favoriteId) || Helpers.isOwnApp(favoriteId)) {
            if (!openSettingsPage("uninstall:" + Helpers.storageId(favoriteId))) {
                settingsMissing();
            }
        } else {
            Qt.openUrlExternally("appstream://" + Helpers.storageId(favoriteId));
        }
        start.close();
    }

    // Store apps from Flathub run in a sandbox whose permissions Settings
    // shows; their launchers come from Flatpak's exports.
    function isSandboxed(favoriteId: string): bool {
        const row = appRow(favoriteId);
        return row >= 0 && start.appIndex.objectAt(row).url.indexOf("/flatpak/exports/") >= 0;
    }

    function favouriteIndex(favoriteId: string): int {
        const pins = Array.from(start.rootModel.favoritesModel.favorites ?? []);
        const found = pins.indexOf(favoriteId);
        return found >= 0 ? found : pins.indexOf("applications:" + favoriteId);
    }

    // The Windows set of app commands (MenusShell.dc "Start, app"): pin to
    // the taskbar, pin or unpin from Start, move a pinned app to the front,
    // then its permissions and uninstall through the Store, last and red.
    // pinnedIndex is the app's place among the pins when the menu was
    // opened on a pinned tile.
    // trigger(actionId, argument) runs one of the app's Kicker actions.
    // The Wild OS taskbar (org.wildos.tasks) is not one of the task managers
    // Plasma's app models know, so Start pins apps through the taskbar's
    // settings, which it watches.
    function taskbarApplet(): var {
        const applets = Plasmoid.containment ? Plasmoid.containment.applets : [];
        for (let i = 0; i < applets.length; i++) {
            if (applets[i].pluginName === "org.wildos.tasks") {
                return applets[i];
            }
        }
        return null;
    }

    function appCommands(favoriteId: string, actionList: var, trigger: var, pinnedIndex: int): var {
        const favourites = start.rootModel.favoritesModel;
        const commands = [];
        const taskbar = favoriteId.endsWith(".desktop") ? taskbarApplet() : null;
        if (taskbar) {
            const url = Helpers.taskbarUrl(favoriteId);
            const launchers = Array.from(taskbar.configuration.launchers);
            if (launchers.indexOf(url) >= 0) {
                commands.push({ text: "Unpin from taskbar", icon: "menu-unpin", run: () => taskbar.configuration.launchers = launchers.filter(l => l !== url) });
            } else {
                commands.push({ text: "Pin to taskbar", icon: "menu-pin", run: () => taskbar.configuration.launchers = launchers.concat(url) });
            }
        }
        if (favoriteId.length > 0) {
            if (favourites.isFavorite(favoriteId)) {
                commands.push({ text: "Unpin from Start", icon: "menu-unpin", run: () => favourites.removeFavorite(favoriteId) });
                const place = pinnedIndex >= 0 ? pinnedIndex : favouriteIndex(favoriteId);
                if (place > 0) {
                    commands.push({ text: "Move to front", icon: "menu-front", run: () => favourites.moveRow(place, 0) });
                }
            } else {
                commands.push({ text: "Pin to Start", icon: "menu-pin", run: () => favourites.addFavorite(favoriteId) });
            }
        }
        const last = [];
        if (hasWildSettings && isSandboxed(favoriteId)) {
            last.push({ text: "App permissions", icon: "shield", run: () => start.openSettingsPage("apppermissions") });
        }
        if (canUninstall(favoriteId)) {
            last.push({ text: "Uninstall", icon: "menu-uninstall", danger: true, run: () => start.uninstallApp(favoriteId) });
        }
        if (commands.length > 0 && last.length > 0) {
            commands.push({ separator: true });
        }
        return commands.concat(last);
    }

    Plasmoid.icon: Plasmoid.configuration.icon
    toolTipMainText: "Start"
    toolTipSubText: ""

    preferredRepresentation: compactRepresentation
    // Start is built when the taskbar loads instead of on the first click,
    // which made that first open stutter while it compiled the menu and read
    // every icon. It resets itself each time it opens (StartMenu's
    // onExpandedChanged), so building it early changes nothing else.
    preloadFullRepresentation: true
    compactRepresentation: StartButton {
        launcher: start
    }
    fullRepresentation: StartMenu {
        launcher: start
    }

    Component.onCompleted: {
        if (Plasmoid.hasOwnProperty("activationTogglesExpanded")) {
            Plasmoid.activationTogglesExpanded = true;
        }
    }
}
