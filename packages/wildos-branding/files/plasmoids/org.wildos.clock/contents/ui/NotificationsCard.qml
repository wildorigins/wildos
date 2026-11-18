pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kitemmodels as KItemModels
import org.kde.notificationmanager as NotificationManager
import WildOS.UI as UI

// The design's notifications card: a "Notifications" heading with "Clear
// all", then the notifications 10 px apart, grouped under the app that sent
// them as Windows groups them: the app with the newest notification first,
// and each app's own newest first. It scrolls when there are more than the
// screen has room for. A right click on one offers to turn that app's
// notifications off.
ColumnLayout {
    id: card

    required property var clock
    property real maxHeight: 600
    property real now: Date.now()

    readonly property var history: clock.history
    readonly property bool empty: history.count === 0

    // Newest at the top. Plasma's model keeps arrival order here, so the
    // card sorts by the time each notification was sent.
    KItemModels.KSortFilterProxyModel {
        id: newestFirst
        sourceModel: card.history
        sortRoleName: "created"
        sortOrder: Qt.DescendingOrder
    }

    // The name of the app behind a row of the sorted list.
    function appAt(index: var): string {
        return String(newestFirst.data(index, NotificationManager.Notifications.ApplicationNameRole) || "");
    }

    // The apps with notifications, in the order their newest arrived.
    property var apps: []

    function regroup(): void {
        let names = [];
        try {
            const rows = newestFirst.rowCount();
            for (let row = 0; row < rows; row++) {
                const name = appAt(newestFirst.index(row, 0));
                if (names.indexOf(name) < 0) {
                    names.push(name);
                }
            }
        } catch (error) {
            // Notifications that cannot be grouped are still listed, under
            // one group with no heading.
            console.warn("Notifications are not grouped:", error);
            names = [""];
        }
        if (names.length !== apps.length || names.some((name, i) => name !== apps[i])) {
            apps = names;
        }
    }

    Connections {
        target: newestFirst
        function onRowsInserted() { regroupTimer.restart(); }
        function onRowsRemoved() { regroupTimer.restart(); }
        function onRowsMoved() { regroupTimer.restart(); }
        function onLayoutChanged() { regroupTimer.restart(); }
        function onModelReset() { regroupTimer.restart(); }
    }

    // Notifications come and go in bursts; one pass covers each burst.
    Timer {
        id: regroupTimer
        interval: 0
        onTriggered: card.regroup()
    }

    Component.onCompleted: regroup()

    // Every button in reading order, for the flyout's keyboard focus.
    function stops(): var {
        if (card.empty) {
            return [dndButton, settingsLink];
        }
        let result = clearAll.visible ? [clearAll] : [];
        for (let g = 0; g < groups.count; g++) {
            const group = groups.itemAt(g);
            if (!group) {
                continue;
            }
            for (let i = 0; i < group.items.count; i++) {
                const item = group.items.itemAt(i);
                if (item) {
                    result = result.concat(item.stops());
                }
            }
        }
        return result;
    }

    // The right-click menu of one notification. It is drawn inside the
    // card: a menu in a window of its own would take the keyboard from the
    // flyout, which closes when it loses it.
    function showMenu(item: var, x: real, y: real): void {
        const n = item.n;
        const desktopEntry = String(n?.desktopEntry || "");
        const name = String(n?.applicationName || "");
        const entries = [];
        // Settings lists apps by their launcher, so only those can be
        // turned back on there.
        if (desktopEntry.length > 0 && name.length > 0) {
            entries.push({ text: qsTr("Turn off notifications from %1").arg(name), icon: "bell", run: () => card.clock.turnOffApp(desktopEntry) });
        }
        entries.push({ text: qsTr("Notification settings"), icon: "menu-settings", run: () => card.clock.openSettings("notifications") });
        menu.entries = entries;
        const point = item.mapToItem(card, x, y);
        menu.x = Math.round(point.x);
        menu.y = Math.round(point.y);
        menu.open();
    }

    // The card's window never has the keyboard, so the flyout hands the
    // menu its keys while it is open.
    function menuKey(key: int): bool {
        if (!menu.visible) {
            return false;
        }
        switch (key) {
        case Qt.Key_Down:
        case Qt.Key_Tab:
            menu.currentIndex = (menu.currentIndex + 1) % menu.count;
            break;
        case Qt.Key_Up:
        case Qt.Key_Backtab:
            menu.currentIndex = (Math.max(menu.currentIndex, 0) + menu.count - 1) % menu.count;
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space: {
            const row = menu.itemAt(menu.currentIndex);
            menu.dismiss();
            if (row) {
                row.triggered();
            }
            break;
        }
        case Qt.Key_Escape:
            menu.dismiss();
            break;
        }
        return true;
    }

    UI.Menu {
        id: menu
        minimumWidth: 220
        maximumWidth: 320
    }

    Connections {
        target: card.clock
        function onExpandedChanged() {
            if (!card.clock.expanded) {
                menu.dismiss();
            }
        }
    }

    // Scrolls the list so the keyboard's button is in view.
    function reveal(button: Item): void {
        let inside = button;
        while (inside && inside !== column) {
            inside = inside.parent;
        }
        if (!inside || !scroller.interactive) {
            return;
        }
        const top = button.mapToItem(column, 0, 0).y;
        const bottom = top + button.height;
        if (top < scroller.contentY) {
            scroller.contentY = Math.max(0, top - 8);
        } else if (bottom > scroller.contentY + scroller.height) {
            scroller.contentY = Math.min(scroller.contentHeight - scroller.height, bottom - scroller.height + 8);
        }
    }

    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        spacing: 8

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Notifications")
            // The empty card is EmptyStates.dc's (15 px); the list is
            // Flyouts.dc's (14 px).
            size: card.empty ? 15 : 14
            font.weight: UI.Style.weightSemiBold
            Accessible.role: Accessible.Heading
        }

        // EmptyStates.dc: with nothing to clear, the corner holds do not
        // disturb instead, quiet until it is on.
        CardButton {
            id: dndButton
            visible: card.empty
            Layout.preferredWidth: dndLabel.implicitWidth + 12
            Layout.preferredHeight: 20
            Layout.rightMargin: -6
            fillHeight: 24
            radius: 6
            hoverColor: "transparent"
            accessibleName: card.clock.doNotDisturb ? qsTr("Turn off do not disturb") : qsTr("Turn on do not disturb")
            Accessible.role: Accessible.CheckBox
            Accessible.checked: card.clock.doNotDisturb
            onClicked: card.clock.setDoNotDisturb(!card.clock.doNotDisturb)

            UI.Label {
                id: dndLabel
                anchors.centerIn: parent
                text: card.clock.doNotDisturb ? qsTr("Do not disturb is on") : qsTr("Do not disturb")
                color: card.clock.doNotDisturb ? UI.Style.accentText : UI.Style.inkSubtle
                font.underline: dndButton.hovered
            }

            MouseArea {
                anchors.centerIn: parent
                width: parent.width
                height: 44
                onClicked: dndButton.activate()
            }
        }

        CardButton {
            id: clearAll
            visible: !card.empty
            Layout.preferredWidth: clearLabel.implicitWidth + 12
            Layout.preferredHeight: 20
            Layout.rightMargin: -6
            fillHeight: 24
            radius: 6
            hoverColor: "transparent"
            accessibleName: qsTr("Clear all notifications")
            onClicked: card.clock.clearAll()

            UI.Label {
                id: clearLabel
                anchors.centerIn: parent
                text: qsTr("Clear all")
                size: 12
                color: UI.Style.accentText
                font.underline: clearAll.hovered
            }

            // A finger-sized target around the short link.
            MouseArea {
                anchors.centerIn: parent
                width: parent.width
                height: 44
                onClicked: clearAll.activate()
            }
        }
    }

    Flickable {
        id: scroller
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(column.implicitHeight, Math.max(120, card.maxHeight - 30))
        visible: !card.empty
        contentHeight: column.implicitHeight
        // Only clipped when it scrolls, so focus rings drawn just outside the
        // cards are not cut off.
        clip: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        T.ScrollBar.vertical: UI.ScrollBar {}

        Column {
            id: column
            width: scroller.width
            spacing: 14

            Repeater {
                id: groups
                model: card.apps

                Column {
                    id: group

                    required property string modelData
                    property alias items: items

                    width: column.width
                    spacing: 10

                    KItemModels.KSortFilterProxyModel {
                        id: ofApp
                        sourceModel: newestFirst
                        filterRowCallback: (row, parent) => card.appAt(newestFirst.index(row, 0, parent)) === group.modelData
                    }

                    Row {
                        x: 4
                        height: 14
                        spacing: 8
                        visible: group.modelData.length > 0 && items.count > 0

                        Kirigami.Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            visible: source !== ""
                            source: items.count > 0 && items.itemAt(0) ? String(items.itemAt(0).n?.applicationIconName || "") : ""
                            roundToIconSize: false
                        }

                        UI.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, group.width - 40)
                            text: group.modelData
                            size: 12
                            font.weight: UI.Style.weightSemiBold
                            color: UI.Style.inkMuted
                            Accessible.role: Accessible.Heading
                        }
                    }

                    Repeater {
                        id: items
                        model: ofApp

                        NotificationItem {
                            id: entry

                            required property var model
                            required property int index

                            width: group.width
                            notification: model
                            now: card.now
                            function sourceIndex(): var {
                                return newestFirst.mapToSource(ofApp.mapToSource(ofApp.index(index, 0)));
                            }

                            onDefaultActionInvoked: card.clock.invokeDefault(card.history, sourceIndex(), !!model.resident)
                            onActionInvoked: name => card.clock.invokeAction(card.history, sourceIndex(), name, !!model.resident)
                            onCloseRequested: card.clock.closeNotification(card.history, sourceIndex())
                            onKillJobRequested: card.history.killJob(sourceIndex())
                            onLinkActivated: link => {
                                Qt.openUrlExternally(link);
                                card.clock.expanded = false;
                            }
                            onMenuRequested: (x, y) => card.showMenu(entry, x, y)
                        }
                    }
                }
            }
        }
    }

    // EmptyStates.dc: a calm answer instead of a blank card, and where to
    // change what may notify.
    ColumnLayout {
        visible: card.empty
        Layout.fillWidth: true
        Layout.topMargin: 14
        Layout.bottomMargin: 2
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            radius: 32
            color: UI.Style.accentSoft

            UI.Glyph {
                anchors.centerIn: parent
                width: 30
                height: 30
                path: "M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15zM10 20a2 2 0 0 0 4 0"
                strokeWidth: 1.9
                color: UI.Style.accentText
            }
        }

        UI.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("You're all caught up")
            size: 15
            font.weight: UI.Style.weightSemiBold
        }

        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(280, card.width)
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("New notifications from your apps will appear here.")
            color: UI.Style.inkMuted
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }

        CardButton {
            id: settingsLink
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            Layout.preferredWidth: settingsLabel.implicitWidth + 12
            Layout.preferredHeight: 24
            radius: 6
            hoverColor: "transparent"
            accessibleName: qsTr("Notification settings")
            Accessible.role: Accessible.Link
            onClicked: card.clock.openSettings("notifications")

            UI.Label {
                id: settingsLabel
                anchors.centerIn: parent
                text: qsTr("Notification settings")
                color: UI.Style.accentText
                font.underline: settingsLink.hovered
            }
        }
    }
}
