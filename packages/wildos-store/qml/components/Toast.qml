import QtQuick
import WildOS.Store
import WildOS.UI as UI

// A short note at the bottom of the window when an install finishes or
// fails, so people can carry on browsing while apps install. Installed
// apps offer Open, failures offer Details.
UI.Toast {
    id: toast

    property string appKey
    property string kind
    readonly property var app: appKey.length > 0 ? Catalog.app(appKey) : null

    signal openRequested(string key)

    function showFor(key, what, message) {
        appKey = key;
        kind = what;
        const a = Catalog.app(key);
        if (!a) {
            return;
        }
        if (what === "installed") {
            show(qsTr("%1 is installed").arg(a.name), a.canOpen ? qsTr("Open") : "");
        } else {
            show(qsTr("Couldn't finish with %1. %2").arg(a.name).arg(message || ""), qsTr("Details"));
        }
    }

    function note(message) {
        appKey = "";
        kind = "note";
        show(message, "");
    }

    // Theme icons and local files draw here; an icon still on its way from
    // the network is left out rather than shown blank.
    themeIcon: {
        const source = app ? app.iconSource : "";
        if (source.startsWith("image://theme/")) {
            return decodeURIComponent(source.slice(14));
        }
        return source.startsWith("file:") ? source : "";
    }
    duration: kind === "failed" ? 9000 : 6000

    onActionClicked: {
        if (kind === "installed" && app) {
            app.open();
        } else if (kind === "failed") {
            openRequested(appKey);
        }
    }
}
