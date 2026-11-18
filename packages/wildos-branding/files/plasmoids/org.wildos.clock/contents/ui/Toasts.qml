pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import org.kde.plasma.plasmoid
import org.kde.notificationmanager as NotificationManager
import WildOS.UI as UI
import org.wildos.overlays

// New notifications as toasts in the top right corner (Overlays.dc): 380
// wide, the flyouts' glass and 22px corners, 18px in from the edges, the
// newest on top and older ones below. Each slides in from the right, stays 6 seconds
// (the pointer on it holds it there), then goes to the notification list in
// the clock flyout. Buttons sit under the text, the main one on the right.
// They follow Plasma's notification settings for which apps may pop up and
// for do not disturb, and stay hidden while the flyout is open, since the
// same notifications are listed there.
Item {
    id: toasts

    required property var clock
    required property var settings

    readonly property int edgeMargin: 18
    // Overlays.dc puts the second toast's top 130px below the first's.
    readonly property int gap: 24

    // Wild OS shows toasts for 6 seconds. Plasma's own default is 5, so a
    // timeout still at that default means nobody chose one.
    readonly property int defaultTimeout: settings.popupTimeout === 5000 ? 6000 : settings.popupTimeout

    // The screen the taskbar is on, which is where Windows shows toasts.
    readonly property var screenInfo: {
        const screens = Qt.application.screens;
        if (Plasmoid.containment) {
            const g = Plasmoid.containment.screenGeometry;
            for (let i = 0; i < screens.length; i++) {
                if (screens[i].virtualX === g.x && screens[i].virtualY === g.y) {
                    return screens[i];
                }
            }
        }
        return screens.length > 0 ? screens[0] : null;
    }

    NotificationManager.Notifications {
        id: popups
        limit: 3
        showExpired: false
        showDismissed: false
        showAddedDuringInhibition: false
        showJobs: toasts.settings.jobsInNotifications
        sortMode: NotificationManager.Notifications.SortByDate
        sortOrder: Qt.DescendingOrder
        groupMode: NotificationManager.Notifications.GroupDisabled
        blacklistedDesktopEntries: toasts.settings.popupBlacklistedApplications
        blacklistedNotifyRcNames: toasts.settings.popupBlacklistedServices
        whitelistedDesktopEntries: toasts.clock.doNotDisturb ? toasts.settings.doNotDisturbPopupWhitelistedApplications : []
        whitelistedNotifyRcNames: toasts.clock.doNotDisturb ? toasts.settings.doNotDisturbPopupWhitelistedServices : []
        urgencies: {
            const dnd = toasts.clock.doNotDisturb;
            let urgencies = 0;
            if (!dnd || toasts.settings.criticalPopupsInDoNotDisturbMode) {
                urgencies |= NotificationManager.Notifications.CriticalUrgency;
            }
            if (!dnd) {
                urgencies |= NotificationManager.Notifications.NormalUrgency;
                if (toasts.settings.lowPriorityPopups) {
                    urgencies |= NotificationManager.Notifications.LowUrgency;
                }
            }
            return urgencies;
        }
    }

    function place(): void {
        const cards = [];
        for (let i = 0; i < windows.count; i++) {
            if (windows.objectAt(i)) {
                cards.push(windows.objectAt(i));
            }
        }
        // The newest sits in the corner and pushes older ones down.
        cards.sort((a, b) => b.created - a.created);
        let top = edgeMargin;
        for (const toast of cards) {
            toast.offset = top;
            top += toast.height + gap;
        }
    }

    Instantiator {
        id: windows
        model: popups

        delegate: Window {
            id: toast

            required property var model
            required property int index
            readonly property int notificationId: model.notificationId
            readonly property real created: model.created ? model.created.getTime() : 0
            readonly property bool critical: model.urgency === NotificationManager.Notifications.CriticalUrgency
            property int offset: toasts.edgeMargin
            // When a toast above goes, the ones left glide up to their new
            // place. A toast still arriving is put straight where it belongs.
            Behavior on offset {
                enabled: toast.visible
                NumberAnimation {
                    duration: UI.Style.motionNormal
                    easing.type: UI.Style.easeStandard
                }
            }
            // The compositor takes the new margin with the window's next
            // frame, and nothing else in the toast changes while it moves.
            onOffsetChanged: toast.update()

            readonly property int timeout: {
                // A copy still going stays up, as Windows' copy window does.
                if (critical || content.runningJob) {
                    return 0;
                }
                const own = model.timeout;
                return own === -1 || own === undefined || own === 0 && !model.resident ? toasts.defaultTimeout : own;
            }

            screen: toasts.screenInfo
            // DialogCopy.dc draws the copy window 520 wide.
            width: content.runningJob ? 520 : 380
            height: content.implicitHeight + 28
            color: "transparent"
            flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
                | (placement.wayland ? 0 : Qt.ToolTip)
            title: content.title
            // Shown once placed (see Component.onCompleted).
            visible: false
            onHeightChanged: Qt.callLater(toasts.place)

            OverlayPlacement {
                id: placement
                window: toast
                edges: OverlayPlacement.Top | OverlayPlacement.Right
                marginTop: toast.offset
                marginRight: toasts.edgeMargin
                // Critical ones stay above full screen apps, as Windows'
                // alarms and calls do.
                layer: toast.critical ? OverlayPlacement.Overlay : OverlayPlacement.AboveWindows
                keyboard: OverlayPlacement.NoKeyboard
                slideFrom: UI.Style.reducedMotion ? OverlayPlacement.NoEdge : OverlayPlacement.Right
                scope: "wildos-toast"
            }

            UI.WindowFrame {
                window: toast
                radius: UI.Style.radius2xl
                shadowColor: UI.Style.shadowWindow
            }

            Rectangle {
                anchors.fill: parent
                radius: UI.Style.radius2xl
                color: UI.Style.glassPanel
                border.width: 1
                border.color: UI.Style.borderGlass
            }

            HoverHandler {
                id: hover
                onHoveredChanged: {
                    if (hovered) {
                        toast.model.read = true;
                    }
                }
            }

            NotificationItem {
                id: content
                x: 16
                y: 14
                width: toast.width - 32
                toast: true
                notification: toast.model
                onDefaultActionInvoked: toasts.clock.invokeDefault(popups, popups.index(toast.index, 0), !!toast.model.resident)
                onActionInvoked: name => toasts.clock.invokeAction(popups, popups.index(toast.index, 0), name, !!toast.model.resident)
                onCloseRequested: toasts.clock.closeNotification(popups, popups.index(toast.index, 0))
                onKillJobRequested: popups.killJob(popups.index(toast.index, 0))
                onLinkActivated: link => {
                    Qt.openUrlExternally(link);
                    toasts.clock.closeNotification(popups, popups.index(toast.index, 0));
                }
            }

            // Waits while the pointer is on the card, so it does not vanish
            // while someone is reading it or reaching for a button; leaving
            // it starts the full time again, as on Windows.
            Timer {
                interval: toast.timeout
                running: toast.timeout > 0 && !hover.hovered && !toasts.clock.expanded && toast.visible
                onTriggered: {
                    // Resident ones keep their buttons working from the list.
                    if (toast.model.resident) {
                        toast.model.expired = true;
                    } else {
                        popups.expire(popups.index(toast.index, 0));
                    }
                }
            }

            Component.onCompleted: {
                // As Plasma does, remember apps seen sending notifications, so they
                // appear in Settings and their notifications are kept in the list.
                if (model.type === NotificationManager.Notifications.NotificationType
                        && model.desktopEntry && !model.notifyRcName) {
                    toasts.settings.registerKnownApplication(model.desktopEntry);
                    toasts.settings.save();
                }
                // This card times itself out, so the model must not as well.
                popups.stopTimeout(popups.index(index, 0));
                toasts.place();
                // The surface is set up by OverlayPlacement before the
                // window first shows.
                Qt.callLater(() => {
                    toast.visible = Qt.binding(() => !toasts.clock.expanded);
                });
            }
        }

        onObjectAdded: Qt.callLater(toasts.place)
        onObjectRemoved: (index, object) => {
            popups.startTimeout(object.notificationId);
            Qt.callLater(toasts.place);
        }
    }
}
