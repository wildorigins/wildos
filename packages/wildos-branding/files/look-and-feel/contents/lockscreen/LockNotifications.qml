pragma Singleton

import QtQuick
import QtCore
import org.kde.notificationmanager as NotificationManager

// Notifications that arrive while the screen is locked. Plasma's popups never
// reach a locked screen (KWin shows only the greeter there), so the lock
// screen asks Plasma's notification server for a copy of each one, through
// the watcher interface it offers for exactly this (Plasma Mobile's lock
// screen uses it). One copy for every screen, since the server talks to one
// watcher per process.
//
// "Quiet lock screen" (Settings > Privacy and security > Advanced protection,
// quiet_lock in /etc/wildos/advanced-protection, on unless set to off) keeps
// only the app's name and icon: the title and text are dropped as they arrive,
// so they are never in the lock screen's memory, let alone on it.
QtObject {
    id: tally

    readonly property bool quiet: store.value("quiet_lock", "on") !== "off"

    // Newest first: notificationId, appName, iconName, and in full mode
    // summary and body.
    readonly property ListModel arrived: ListModel {}

    readonly property var appIcons: {
        const seen = [];
        for (let i = 0; i < arrived.count && seen.length < 3; i++) {
            const icon = arrived.get(i).iconName;
            if (seen.indexOf(icon) < 0)
                seen.push(icon);
        }
        return seen;
    }

    readonly property Settings store: Settings {
        location: "file:///etc/wildos/advanced-protection"
    }

    readonly property NotificationManager.WatchedNotificationsModel watcher: NotificationManager.WatchedNotificationsModel {}

    function role(row, name) {
        return watcher.data(watcher.index(row, 0), NotificationManager.Notifications[name]);
    }

    function entry(row) {
        const icon = role(row, "ApplicationIconNameRole") || role(row, "IconNameRole") || "preferences-desktop-notification-bell";
        const item = {
            notificationId: role(row, "IdRole"),
            appName: role(row, "ApplicationNameRole") || "",
            iconName: icon,
            summary: "",
            body: "",
        };
        if (!quiet) {
            item.summary = role(row, "SummaryRole") || "";
            item.body = role(row, "BodyRole") || "";
        }
        return item;
    }

    function indexOf(notificationId) {
        for (let i = 0; i < arrived.count; i++) {
            if (arrived.get(i).notificationId === notificationId)
                return i;
        }
        return -1;
    }

    // The watcher drops a notification when its popup would have timed out,
    // so what arrived is kept here instead, for as long as the screen stays
    // locked. An app that updates a notification (a download, a chat) changes
    // its entry rather than adding another.
    readonly property Connections watcherEvents: Connections {
        target: tally.watcher

        function onRowsInserted(parent, first, last) {
            for (let row = first; row <= last; row++) {
                const item = tally.entry(row);
                const at = tally.indexOf(item.notificationId);
                if (at >= 0)
                    tally.arrived.remove(at);
                tally.arrived.insert(0, item);
            }
        }

        function onDataChanged(topLeft, bottomRight) {
            for (let row = topLeft.row; row <= bottomRight.row; row++) {
                const item = tally.entry(row);
                const at = tally.indexOf(item.notificationId);
                if (at >= 0)
                    tally.arrived.set(at, item);
            }
        }
    }
}
