import QtQuick
import QtQml
import org.kde.kwin as KWin
import org.wildos.shell as WildOS

// The taskbar underglow (UnderglowOptions board): a 2px line along the top
// edge of the docked taskbar with a soft glow around it, in the accent, a
// custom colour or a drifting spectrum, as chosen in Settings. On the
// floating taskbar each pill gets its own along its top edge (#89).
//
// KWin draws it, in a strip window of its own laid over the taskbar's top
// edge. Breathing and flowing then repaint only that strip inside the
// compositor and never make plasmashell redraw the panel. It is a KWin script
// rather than a C++ effect because the scripting API it uses holds across
// KWin releases, where a binary effect must be rebuilt for each one.
Item {
    id: root

    // KWin reports the glow windows like any other; this title is how they
    // are told apart and kept out of Alt+Tab, Overview and the taskbar.
    readonly property string glowTitle: "Wild OS taskbar underglow"

    // The docked taskbars, one per screen that has one.
    property var docks: []
    // What to light: each docked bar whole, or each of a floating bar's
    // pills, as {dock, pill, radius, key}.
    property var strips: []
    // The ones a full screen window is stacked over, whose glow would
    // otherwise sit on top of the game or video.
    property var covered: []

    // Signing in, KWin is up before the start-up screen (ksplash) and the
    // taskbar exists underneath it, so on its own the line would show over
    // that screen and through the fade. It waits until the start-up screen
    // has gone and the desktop has faded in, then fades in itself. When
    // the script starts on a desktop already showing (KWin restarted, the
    // script reloaded) there is no start-up screen and it shows straight
    // away.
    property bool splashSeen: false
    property bool splashUp: false
    property bool ready: false

    function isSplash(w: var): bool {
        return !w.deleted && (w.resourceClass === "ksplashqml" || w.resourceName === "ksplashqml");
    }

    Timer {
        id: readyTimer
        onTriggered: root.ready = true
    }

    function onCurrentDesktop(w: var): bool {
        return w.onAllDesktops || w.desktops.indexOf(KWin.Workspace.currentDesktop) >= 0;
    }

    function onScreen(w: var): bool {
        return !w.deleted && !w.minimized && !w.hidden && onCurrentDesktop(w);
    }

    // Plasma's panel is a dock window. The taskbar runs the full width of
    // its screen along the bottom edge, docked or floating (a floating
    // taskbar is a taller window with the pills drawn inside it). A panel at
    // another edge is not the taskbar.
    function isDockedTaskbar(w: var): bool {
        if (!w || w.deleted || !w.dock || !w.output) {
            return false;
        }
        const g = w.frameGeometry;
        const s = w.output.geometry;
        return g.x <= s.x + 1 && g.x + g.width >= s.x + s.width - 1
            && Math.abs(g.y + g.height - (s.y + s.height)) <= 1
            && g.height * 4 < g.width;
    }

    function sameList(a: var, b: var): bool {
        return a.length === b.length && a.every((w, i) => w === b[i]);
    }

    function updateStrips(): void {
        const next = [];
        for (const dock of docks) {
            if (!WildOS.TaskbarStyle.floating) {
                next.push({ dock: dock, pill: Qt.rect(0, 0, 0, 0), radius: 0, key: dock.internalId + "" });
                continue;
            }
            const screen = dock.output.name;
            const radius = WildOS.Underglow.pillRadius(screen);
            for (const pill of WildOS.Underglow.pills(screen)) {
                next.push({ dock: dock, pill: pill, radius: radius,
                            key: [dock.internalId, pill.x, pill.y, pill.width, pill.height, radius].join() });
            }
        }
        // Kept as they are unless something moved, so the glow windows are
        // not remade on every refresh.
        if (next.map(s => s.key).join("|") !== strips.map(s => s.key).join("|")) {
            strips = next;
        }
    }

    Connections {
        target: WildOS.Underglow
        function onPillsChanged(): void {
            root.updateStrips();
        }
    }

    Connections {
        target: WildOS.TaskbarStyle
        function onFloatingChanged(): void {
            root.updateStrips();
        }
    }

    function refresh(): void {
        const windows = KWin.Workspace.stackingOrder;
        const found = windows.filter(isDockedTaskbar);
        if (!sameList(found, docks)) {
            docks = found;
        }
        updateStrips();
        const fullScreen = windows.filter(w => w.fullScreen && onScreen(w));
        WildOS.Underglow.fullScreen = fullScreen.length > 0;
        const over = found.filter(d => fullScreen.some(f => f.output === d.output && f.stackingOrder > d.stackingOrder));
        if (!sameList(over, covered)) {
            covered = over;
        }
        const splash = windows.some(isSplash);
        if (splash && !splashUp) {
            splashSeen = true;
            ready = false;
            readyTimer.stop();
        } else if (!splash && splashUp) {
            // After the desktop's own fade in.
            readyTimer.interval = 700;
            readyTimer.restart();
        }
        splashUp = splash;
    }

    Timer {
        id: refreshTimer
        interval: 0
        onTriggered: root.refresh()
    }

    function refreshLater(): void {
        refreshTimer.restart();
    }

    function watch(w: var): void {
        if (w.caption === glowTitle) {
            w.skipSwitcher = true;
            w.skipTaskbar = true;
            w.skipPager = true;
            return;
        }
        w.fullScreenChanged.connect(refreshLater);
        w.minimizedChanged.connect(refreshLater);
        w.hiddenChanged.connect(refreshLater);
        w.outputChanged.connect(refreshLater);
        w.desktopsChanged.connect(refreshLater);
        w.stackingOrderChanged.connect(refreshLater);
        // Only a dock can become or stop being the taskbar by moving, and
        // watching every window would refresh on each frame of a drag.
        if (w.dock) {
            w.frameGeometryChanged.connect(refreshLater);
        }
    }

    Connections {
        target: KWin.Workspace

        function onWindowAdded(window: var): void {
            root.watch(window);
            root.refreshLater();
        }

        function onWindowRemoved(): void {
            root.refreshLater();
        }

        function onWindowActivated(): void {
            root.refreshLater();
        }

        function onCurrentDesktopChanged(): void {
            root.refreshLater();
        }

        function onScreensChanged(): void {
            root.refreshLater();
        }
    }

    Instantiator {
        model: root.strips
        delegate: Glow {
            dock: modelData.dock
            pill: modelData.pill
            radius: modelData.radius
            covered: root.covered.indexOf(modelData.dock) >= 0
            ready: root.ready
            title: root.glowTitle
        }
    }

    Component.onCompleted: {
        for (const w of KWin.Workspace.stackingOrder) {
            watch(w);
        }
        refresh();
        // Started with the session, before any start-up screen: give it a
        // moment to appear. Started on a running desktop: show now.
        if (!splashUp) {
            readyTimer.interval = KWin.Workspace.stackingOrder.some(w => w.dock) ? 0 : 4000;
            readyTimer.start();
        }
    }
}
