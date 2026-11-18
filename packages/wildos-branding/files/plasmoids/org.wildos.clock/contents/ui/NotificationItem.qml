pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.notificationmanager as NotificationManager
import WildOS.UI as UI
import "Formats.js" as Formats

// One notification as the design draws it: a 20 px icon, a 13 px semibold
// title with the time on the right, a 12 px body, and any actions as a row of
// buttons, the last one filled. Notifications asking for a decision (with
// actions, or critical) sit on the accent tint; the rest on a light fill.
// In a pop-up toast the window's glass is the card, so no fill is drawn, and
// the app's name sits over the title; in the list the group's heading names
// the app. A body longer than its few lines gets a chevron that opens it.
CardButton {
    id: item

    required property var notification
    // The model row goes away a moment before this item does.
    readonly property var n: notification ? notification : ({})
    property bool toast: false
    property real now: Date.now()
    // Toasts draw a 24px icon (Overlays.dc), the list 20px (Flyouts.dc).
    readonly property int iconSize: toast ? 24 : 20

    readonly property bool isJob: n?.type === NotificationManager.Notifications.JobType
    // A copy still going is drawn by JobProgress (DialogCopy.dc); a finished
    // one is an ordinary notification again.
    readonly property bool runningJob: isJob && n?.jobState !== NotificationManager.Notifications.JobStateStopped
    readonly property var actionNames: n?.actionNames || []
    readonly property var actionLabels: n?.actionLabels || []
    readonly property bool tinted: actionNames.length > 0
        || n?.urgency === NotificationManager.Notifications.CriticalUrgency
    readonly property bool hasDefault: !!n?.hasDefaultAction
    // The keyboard is on this notification or one of its buttons.
    readonly property bool keyboardInside: Nav.keyboard && Nav.current !== null
        && (Nav.current === item || Nav.current === closeButton || Nav.current === cancelJob
            || Nav.current === expandButton || Nav.current.parent === actionsGrid)
    readonly property var iconSource: n?.image || n?.iconName || n?.applicationIconName || ""
    readonly property string appName: String(n?.applicationName || "")
    readonly property string title: {
        const summary = String(n?.summary || "").trim();
        return summary.length > 0 ? summary : appName;
    }
    // The whole body instead of its first few lines.
    property bool expanded: false

    // Every button in the item, in reading order, for keyboard focus.
    function stops(): var {
        if (runningJob) {
            return jobProgress.item ? jobProgress.item.stops() : [];
        }
        const list = [];
        if (hasDefault) {
            list.push(item);
        }
        if (expandButton.visible) {
            list.push(expandButton);
        }
        for (let i = 0; i < actionRepeater.count; i++) {
            list.push(actionRepeater.itemAt(i));
        }
        if (cancelJob.visible) {
            list.push(cancelJob);
        }
        if (n?.closable !== false && !isJob) {
            list.push(closeButton);
        }
        return list.filter(b => b !== null);
    }

    signal defaultActionInvoked()
    signal actionInvoked(string name)
    signal closeRequested()
    signal killJobRequested()
    signal linkActivated(string link)
    // A right click, or the Menu key, at this point on the item.
    signal menuRequested(real x, real y)

    function requestMenu(): void {
        menuRequested(width / 2, height / 2);
    }

    implicitHeight: (runningJob ? jobProgress.implicitHeight : content.implicitHeight) + (toast ? 0 : 28)
    radius: Metrics.itemRadius
    clickable: hasDefault && !runningJob
    baseColor: toast ? "transparent" : tinted ? UI.Style.accentSoft : UI.Style.surfaceControl
    hoverColor: toast ? UI.Style.tone(0.04) : tinted ? UI.Style.fillSelected(0.16, 0.13) : UI.Style.surfaceHover
    accessibleName: (appName.length > 0 && appName !== title ? appName + ". " : "") + title + ". " + body.text.replace(/<[^>]*>/g, "")
    onClicked: defaultActionInvoked()

    function ago(time: var): string {
        if (!time) {
            return "";
        }
        const minutes = Math.floor((now - time.getTime()) / 60000);
        if (minutes < 1) {
            return qsTr("now");
        }
        if (minutes < 60) {
            return qsTr("%1 min").arg(minutes);
        }
        const hours = Math.floor(minutes / 60);
        if (hours < 24) {
            return qsTr("%1 hr").arg(hours);
        }
        return hours < 48 ? qsTr("Yesterday") : Formats.shortDate(time);
    }

    Rectangle {
        anchors.fill: parent
        visible: item.tinted && !item.toast
        radius: Metrics.itemRadius
        color: "transparent"
        border.width: 1
        border.color: UI.Style.accentBorder
    }

    HoverHandler {
        id: hover
    }

    TapHandler {
        enabled: !item.toast
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => item.menuRequested(eventPoint.position.x, eventPoint.position.y)
    }

    Loader {
        id: jobProgress
        x: item.toast ? 0 : 14
        y: item.toast ? 0 : 14
        width: parent.width - (item.toast ? 0 : 28)
        active: item.runningJob
        visible: active
        sourceComponent: JobProgress {
            n: item.n
            toast: item.toast
            onKillRequested: item.killJobRequested()
        }
    }

    ColumnLayout {
        id: content
        x: item.toast ? 0 : 14
        y: item.toast ? 0 : 14
        width: parent.width - (item.toast ? 0 : 28)
        visible: !item.runningJob
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Kirigami.Icon {
                Layout.preferredWidth: item.iconSize
                Layout.preferredHeight: item.iconSize
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: item.toast ? 0 : 1
                visible: item.iconSource !== ""
                source: item.iconSource
                roundToIconSize: false
            }

            UI.Glyph {
                Layout.preferredWidth: item.iconSize
                Layout.preferredHeight: item.iconSize
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: item.toast ? 0 : 1
                visible: item.iconSource === ""
                path: "M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15zM10 20.5a2 2 0 0 0 4 0"
                color: UI.Style.iconInk
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                UI.Label {
                    Layout.fillWidth: true
                    visible: item.toast && item.appName.length > 0 && item.appName !== item.title
                    text: item.appName
                    size: 11
                    color: UI.Style.inkMuted
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    UI.Label {
                        Layout.fillWidth: true
                        text: item.title
                        font.weight: UI.Style.weightSemiBold
                    }

                    CardButton {
                        id: expandButton
                        visible: body.visible && (body.truncated || item.expanded)
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 16
                        fillWidth: 22
                        fillHeight: 22
                        radius: 6
                        accessibleName: item.expanded ? qsTr("Show less") : qsTr("Show more")
                        onClicked: item.expanded = !item.expanded

                        UI.Icon {
                            anchors.centerIn: parent
                            name: item.expanded ? "chevron-up" : "chevron-down"
                            size: 12
                            strokeWidth: 2.4
                            color: UI.Style.inkMuted
                        }

                        // A finger-sized target around the small chevron.
                        MouseArea {
                            anchors.centerIn: parent
                            width: parent.width
                            height: 44
                            onClicked: expandButton.activate()
                        }
                    }

                    UI.Label {
                        visible: !closeButton.visible
                        text: item.isJob ? (item.n?.percentage || 0) + "%" : item.ago(item.n?.updated || item.n?.created)
                        size: 11
                        color: UI.Style.inkMuted
                    }

                    // Stands in for the time while the pointer is on the card.
                    CardButton {
                        id: closeButton
                        visible: item.n?.closable !== false && !item.isJob
                            && (hover.hovered || item.keyboardInside)
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 16
                        fillWidth: 22
                        fillHeight: 22
                        radius: 6
                        accessibleName: qsTr("Dismiss %1").arg(item.title)

                        UI.Glyph {
                            anchors.centerIn: parent
                            width: 12
                            height: 12
                            path: "M6 6l12 12M18 6L6 18"
                            strokeWidth: 2.4
                            color: UI.Style.inkMuted
                        }

                        // A finger-sized target around the small cross.
                        MouseArea {
                            anchors.centerIn: parent
                            width: 44
                            height: 44
                            onClicked: item.closeRequested()
                        }

                        onClicked: item.closeRequested()
                    }
                }

                UI.Label {
                    id: body
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: item.n?.body || ""
                    color: UI.Style.inkStatus
                    size: 12
                    // The design sets this body at 1.45 line height (17 px lines) and
                    // the others at the font's natural spacing.
                    lineHeightMode: item.tinted ? Text.FixedHeight : Text.ProportionalHeight
                    lineHeight: item.tinted ? 17 : 1
                    wrapMode: Text.Wrap
                    maximumLineCount: item.expanded ? (item.toast ? 15 : 30) : item.toast ? 3 : 4
                    elide: Text.ElideRight
                    textFormat: Text.StyledText
                    linkColor: UI.Style.accentText
                    onLinkActivated: link => item.linkActivated(link)
                }

                // Copying or downloading: how far along it is.
                Item {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Layout.preferredHeight: 4
                    visible: item.isJob

                    Rectangle {
                        anchors.fill: parent
                        radius: 2
                        color: UI.Style.surfaceTrack
                    }

                    Rectangle {
                        width: parent.width * Math.min(100, item.n?.percentage || 0) / 100
                        height: parent.height
                        radius: 2
                        color: UI.Style.accent
                    }
                }
            }
        }

        GridLayout {
            id: actionsGrid
            Layout.fillWidth: true
            Layout.leftMargin: item.iconSize + 12
            Layout.topMargin: -6
            Layout.bottomMargin: -6
            visible: actionRepeater.count > 0 || cancelJob.visible
            columns: Math.max(1, Math.min(actionRepeater.count + (cancelJob.visible ? 1 : 0), 2))
            columnSpacing: 8
            rowSpacing: -4

            Repeater {
                id: actionRepeater
                model: item.actionLabels.length

                CardAction {
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: item.actionLabels[index] || ""
                    primary: index === item.actionLabels.length - 1 && item.actionLabels.length > 1
                    onClicked: item.actionInvoked(item.actionNames[index])
                }
            }

            CardAction {
                id: cancelJob
                visible: item.isJob && !!item.n?.killable
                    && item.n?.jobState !== NotificationManager.Notifications.JobStateStopped
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("Cancel")
                onClicked: item.killJobRequested()
            }
        }
    }
}
