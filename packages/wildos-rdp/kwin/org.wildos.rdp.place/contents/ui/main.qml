import QtQuick
import org.kde.kwin as KWin

// A Windows app opens where it was last time (board 9.11). Wayland leaves
// placing windows to the compositor, so this script does it for wildos-rdp:
// the client keeps each app's place between sessions (Placements and
// SavePlacement on org.wildos.RemoteApp), and this puts the app's first
// window there as it opens and reports where it is moved to.
Item {
    id: root

    property var places: ({})
    property bool loaded: false
    property var waiting: []

    function appOf(window) {
        const id = String(window.desktopFileName || window.resourceClass || "");
        // Windows apps the agent knows (wildos-winmode-*) and ones it does
        // not (windows-*, named after their program by wildos-rdp).
        return id.startsWith("wildos-winmode-") || id.startsWith("windows-") ? id : "";
    }

    function placeable(window) {
        // A full screen window has no place of its own: it is the screen.
        return window && !window.deleted && window.normalWindow && !window.transient && !window.fullScreen && root.appOf(window) !== "";
    }

    function openFor(app, except) {
        return KWin.Workspace.windows.some(w => w !== except && !w.deleted && root.appOf(w) === app && w.normalWindow);
    }

    Component {
        id: callComponent
        KWin.DBusCall {
            service: "org.wildos.RemoteApp"
            path: "/org/wildos/RemoteApp"
            dbusInterface: "org.wildos.RemoteApp"
        }
    }

    function call(method, args, done) {
        const c = callComponent.createObject(root, { method: method, arguments: args });
        c.finished.connect(values => {
            if (done) {
                done(values);
            }
            c.destroy();
        });
        c.failed.connect(() => c.destroy());
        c.call();
    }

    function place(window) {
        const p = root.places[root.appOf(window)];
        if (!p || root.openFor(root.appOf(window), window)) {
            return;
        }
        const g = window.frameGeometry;
        let x = p.x;
        let y = p.y;
        // Never off screen: a monitor that has gone since brings it back.
        const area = KWin.Workspace.clientArea(KWin.Workspace.PlacementArea, window);
        if (area && area.width > 0) {
            x = Math.max(area.x, Math.min(x, area.x + area.width - Math.min(g.width, area.width)));
            y = Math.max(area.y, Math.min(y, area.y + area.height - Math.min(g.height, area.height)));
        }
        window.frameGeometry = Qt.rect(x, y, g.width, g.height);
    }

    function remember(window) {
        if (!root.placeable(window)) {
            return;
        }
        const g = window.frameGeometry;
        const app = root.appOf(window);
        root.places[app] = { x: Math.round(g.x), y: Math.round(g.y) };
        root.call("SavePlacement", [app, Math.round(g.x), Math.round(g.y)]);
    }

    function track(window) {
        window.frameGeometryChanged.connect(() => {
            if (window.move || window.resize || window.deleted) {
                return;
            }
            Qt.callLater(() => root.remember(window));
        });
        window.interactiveMoveResizeFinished.connect(() => root.remember(window));
    }

    Connections {
        target: KWin.Workspace

        function onWindowAdded(window) {
            if (!root.placeable(window)) {
                return;
            }
            root.track(window);
            if (root.loaded) {
                root.place(window);
                return;
            }
            root.waiting.push(window);
            root.call("Placements", [], values => {
                try {
                    root.places = JSON.parse(values[0]);
                } catch (e) {
                    root.places = {};
                }
                root.loaded = true;
                const pending = root.waiting;
                root.waiting = [];
                pending.forEach(w => {
                    if (!w.deleted) {
                        root.place(w);
                    }
                });
            });
        }
    }
}
