import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// The folder pane (MailMicrosoft.dc, 236px): Favourites, then each account
// with its folders, "Create new folder", and what Mail kept out this week.
Item {
    id: pane

    signal addAccount()
    signal createFolder(string accountId)
    signal openPrivacy()
    // `from` is the row when the menu was asked for with the keyboard, so
    // it opens beside the row and not wherever the pointer was left.
    signal folderMenu(string accountId, string fullName, string name, string role, var from)
    signal crossAccount()

    // An account being dragged to another place among the accounts, and
    // the account it would land beside. Nothing here uses Drag or DropArea,
    // so it can't be mistaken for a message on its way to a folder.
    property string dragAccount: ""
    property string dragName: ""
    property string dropAccount: ""
    property bool dropBelow: false
    property point dragPoint
    property real dragListY: 0

    function moveAccountBy(accountId, step) {
        const to = MailEngine.accountIndex(accountId) + step;
        if (to < 0 || to >= MailEngine.accounts.length) {
            return;
        }
        MailEngine.moveAccount(accountId, to);
        list.currentIndex = MailEngine.folders.headerRow(accountId);
    }

    // The dragged account takes the place of the one under the pointer.
    function dragOver(listY) {
        const from = MailEngine.accountIndex(dragAccount);
        let to = MailEngine.accounts.length - 1;
        if (listY < 0) {
            to = 0;
        } else {
            const under = list.itemAt(list.width / 2, list.contentY + listY);
            if (under) {
                to = under.accountId.length > 0 ? MailEngine.accountIndex(under.accountId) : 0;
            }
        }
        if (from < 0 || to < 0 || to === from) {
            dropAccount = "";
            return;
        }
        dropAccount = MailEngine.accounts[to].id;
        dropBelow = to > from;
    }

    function endDrag(commit) {
        const moved = dragAccount;
        const beside = dropAccount;
        dragAccount = "";
        dropAccount = "";
        if (commit && beside.length > 0) {
            MailEngine.moveAccount(moved, MailEngine.accountIndex(beside));
            list.currentIndex = MailEngine.folders.headerRow(moved);
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: pane.dragAccount.length > 0
        onActivated: pane.endDrag(false)
    }

    // Holding the account near the top or bottom edge scrolls the list.
    Timer {
        interval: 16
        repeat: true
        running: pane.dragAccount.length > 0 && (pane.dragListY < 28 || pane.dragListY > list.height - 28)
        onTriggered: {
            const top = list.originY;
            const bottom = Math.max(top, top + list.contentHeight - list.height);
            list.contentY = Math.max(top, Math.min(bottom, list.contentY + (pane.dragListY < 28 ? -8 : 8)));
            pane.dragOver(pane.dragListY);
        }
    }

    function iconFor(role) {
        switch (role) {
        case "inbox": return "inbox";
        case "sent": return "sent";
        case "drafts": return "drafts";
        case "archive": return "archive";
        case "trash": return "trash";
        case "junk": return "junk";
        case "outbox": return "outbox";
        }
        return "folder";
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 0

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: MailEngine.folders
            boundsBehavior: Flickable.StopAtBounds
            // Rows scrolled out of sight are let go, and the row being
            // dragged holds the pointer, so it has to outlive the scrolling.
            cacheBuffer: pane.dragAccount.length > 0 ? 20000 : 320
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Folders")
            QQC.ScrollBar.vertical: UI.ScrollBar {}

            Keys.onPressed: function (event) {
                const item = list.currentItem;
                if (!item) {
                    return;
                }
                const account = item.kind === "header" && item.accountId.length > 0;
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    item.activate();
                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    item.openMenu(item);
                } else if (account && (event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Up) {
                    pane.moveAccountBy(item.accountId, -1);
                } else if (account && (event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Down) {
                    pane.moveAccountBy(item.accountId, 1);
                } else if (item.kind === "header" && ((event.key === Qt.Key_Left && item.expanded) || (event.key === Qt.Key_Right && !item.expanded))) {
                    item.activate();
                } else {
                    return;
                }
                event.accepted = true;
            }

            delegate: Item {
                id: row

                required property int index
                required property string kind
                required property string accountId
                required property string fullName
                required property string name
                required property string folderRole
                required property int depth
                required property int count
                required property bool expanded

                readonly property bool current: kind === "folder" && !MailEngine.searching
                    && (accountId === "" ? MailEngine.folderAccount === "" && MailEngine.folderRole === fullName
                                         : MailEngine.folderAccount === accountId && MailEngine.folderName === fullName)

                readonly property bool movable: kind === "header" && accountId.length > 0 && MailEngine.accounts.length > 1
                readonly property bool lastOfAccount: kind === "create" || (kind === "header" && !expanded)

                function activate() {
                    if (kind === "header") {
                        MailEngine.toggleExpanded(accountId);
                    } else if (kind === "create") {
                        pane.createFolder(accountId);
                    } else {
                        MailEngine.openFolder(accountId, fullName);
                    }
                }
                function openMenu(from) {
                    if (kind === "folder" && accountId.length > 0 && folderRole !== "outbox") {
                        pane.folderMenu(accountId, fullName, name, folderRole, from);
                    } else if (movable) {
                        accountMenu.accountId = accountId;
                        if (from) {
                            accountMenu.popup(from, 16, from.height);
                        } else {
                            accountMenu.popup();
                        }
                    }
                }

                width: list.width
                height: kind === "header" ? 36 : kind === "create" ? 30 : 32
                Accessible.role: kind === "create" ? Accessible.Button : Accessible.ListItem
                Accessible.name: kind === "folder" && count > 0 ? qsTr("%1, %2").arg(name).arg(count) : name
                Accessible.selected: current

                Component.onDestruction: {
                    if (mouse.dragged && pane.dragAccount === accountId) {
                        pane.endDrag(false);
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    anchors.topMargin: row.kind === "header" ? 4 : 0
                    radius: UI.Style.radiusXs
                    visible: row.kind === "folder"
                    color: drop.containsDrag || row.current ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                    border.width: drop.containsDrag ? 1 : 0
                    border.color: UI.Style.accent
                    Behavior on color {
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }
                }

                // Messages dragged from the list move here (MessageRowItem).
                DropArea {
                    id: drop
                    anchors.fill: parent
                    enabled: row.kind === "folder" && row.accountId.length > 0 && row.folderRole !== "outbox"
                    keys: ["wildos-mail-messages"]
                    onDropped: function (event) {
                        const keys = event.source.keys || [];
                        const here = keys.filter(k => k.split("\x1f")[0] === row.accountId);
                        if (here.length < keys.length) {
                            pane.crossAccount();
                        }
                        if (here.length === 0) {
                            return;
                        }
                        if (row.folderRole === "trash") {
                            MailEngine.removeKeys(here);
                        } else if (row.folderRole === "junk") {
                            MailEngine.junkKeys(here);
                        } else {
                            MailEngine.moveKeys(here, row.fullName);
                        }
                        event.accept();
                    }
                }

                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    anchors.topMargin: row.kind === "header" ? 4 : 0
                    visible: list.activeFocus && row.ListView.isCurrentItem

                    // Drawn inside the row: the rows touch, and the list
                    // clips at its edges.
                    UI.FocusRing {
                        anchors.margins: 0
                        targetRadius: UI.Style.radiusXs - 4
                    }
                }

                // Where a dragged account would land.
                Rectangle {
                    visible: pane.dropAccount.length > 0 && pane.dropAccount === row.accountId
                        && (pane.dropBelow ? row.lastOfAccount : row.kind === "header")
                    x: 10
                    y: pane.dropBelow ? parent.height - 2 : 1
                    width: parent.width - 20
                    height: 2
                    radius: 1
                    color: UI.Style.accent
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: pane.dragAccount.length > 0 ? Qt.DragMoveCursor : Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Without this the list takes the pointer to scroll, and
                    // an account can't be dragged.
                    preventStealing: row.movable
                    property point pressedAt
                    property bool dragged: false
                    onPressed: function (event) {
                        pressedAt = Qt.point(event.x, event.y);
                        dragged = false;
                    }
                    onPositionChanged: function (event) {
                        if (!row.movable || !(pressedButtons & Qt.LeftButton)) {
                            return;
                        }
                        if (!dragged) {
                            if (Math.abs(event.x - pressedAt.x) + Math.abs(event.y - pressedAt.y) <= 10) {
                                return;
                            }
                            dragged = true;
                            pane.dragName = row.name;
                            pane.dragAccount = row.accountId;
                        }
                        if (pane.dragAccount !== row.accountId) {
                            return;
                        }
                        pane.dragPoint = mouse.mapToItem(pane, event.x, event.y);
                        pane.dragListY = mouse.mapToItem(list, event.x, event.y).y;
                        pane.dragOver(pane.dragListY);
                    }
                    onReleased: {
                        if (dragged && pane.dragAccount === row.accountId) {
                            pane.endDrag(true);
                        }
                    }
                    onCanceled: {
                        if (dragged && pane.dragAccount === row.accountId) {
                            pane.endDrag(false);
                        }
                    }
                    onClicked: function (event) {
                        if (dragged) {
                            return;
                        }
                        list.currentIndex = row.index;
                        if (event.button === Qt.RightButton) {
                            row.openMenu(null);
                        } else {
                            row.activate();
                        }
                    }
                }

                // Account or Favourites heading.
                RowLayout {
                    visible: row.kind === "header"
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 4
                    spacing: 8
                    opacity: pane.dragAccount.length > 0 && pane.dragAccount === row.accountId ? 0.4 : 1

                    MailIcon {
                        name: row.expanded ? "chevron-down" : "chevron-right"
                        size: 13
                        color: UI.Style.inkMuted
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: row.name
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    visible: row.kind === "folder"
                    anchors.fill: parent
                    anchors.leftMargin: 16 + 18 + Math.max(0, row.depth - 1) * 14
                    anchors.rightMargin: 16
                    spacing: 10

                    MailIcon {
                        name: pane.iconFor(row.folderRole)
                        size: 16
                        color: row.current ? UI.Style.iconInk : UI.Style.iconMuted
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: row.name
                        size: 13
                        font.weight: row.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        elide: Text.ElideRight
                    }
                    UI.Label {
                        visible: row.count > 0
                        text: row.count
                        size: 12
                        font.weight: UI.Style.weightSemiBold
                        color: UI.Style.inkMuted
                    }
                }

                UI.Label {
                    visible: row.kind === "create"
                    anchors.verticalCenter: parent.verticalCenter
                    x: 44
                    text: row.name
                    size: 13
                    color: mouse.containsMouse ? UI.Style.accentHover : UI.Style.accent
                }
            }

            footer: Item {
                width: list.width
                height: 38

                MouseArea {
                    id: addMouse
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: pane.addAccount()
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    spacing: 8

                    MailIcon {
                        name: "plus"
                        size: 15
                        color: UI.Style.accent
                    }
                    UI.Label {
                        text: qsTr("Add account")
                        size: 13
                        color: addMouse.containsMouse ? UI.Style.accentHover : UI.Style.accent
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // What Mail kept away from senders this week.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 12
            Layout.topMargin: 8
            implicitHeight: trackers.implicitHeight + 22
            radius: UI.Style.radiusPanel
            color: UI.Style.surfaceRest
            border.width: 1
            border.color: UI.Style.borderSubtle

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: pane.openPrivacy()
            }

            RowLayout {
                id: trackers
                anchors.fill: parent
                anchors.margins: 11
                anchors.leftMargin: 12
                spacing: 10

                MailIcon {
                    name: "shield-check"
                    size: 18
                    color: UI.Style.success
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    UI.Label {
                        Layout.fillWidth: true
                        text: MailEngine.trackersThisWeek === 1 ? qsTr("1 tracker blocked") : qsTr("%1 trackers blocked").arg(MailEngine.trackersThisWeek)
                        size: 12
                        font.weight: UI.Style.weightSemiBold
                        elide: Text.ElideRight
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("This week, across all mail")
                        size: 11
                        color: UI.Style.inkMuted
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // The account's name, following the pointer while it is dragged.
    Rectangle {
        visible: pane.dragAccount.length > 0
        x: Math.max(6, Math.min(pane.width - width - 6, pane.dragPoint.x + 12))
        y: pane.dragPoint.y + 10
        z: 10
        width: Math.min(dragLabel.implicitWidth + 24, pane.width - 12)
        height: 30
        radius: UI.Style.radiusXs
        color: UI.Style.windowOpaque
        border.width: 1
        border.color: UI.Style.borderSubtle

        UI.Label {
            id: dragLabel
            x: 12
            width: parent.width - 24
            anchors.verticalCenter: parent.verticalCenter
            text: pane.dragName
            size: 13
            font.weight: UI.Style.weightSemiBold
            elide: Text.ElideRight
        }
    }

    UI.Menu {
        id: accountMenu
        property string accountId
        entries: {
            const accounts = MailEngine.accounts;
            const at = accounts.findIndex(a => a.id === accountMenu.accountId);
            return [
                { text: qsTr("Move up"), icon: "chevron-up", enabled: at > 0, run: () => pane.moveAccountBy(accountMenu.accountId, -1) },
                { text: qsTr("Move down"), icon: "chevron-down", enabled: at >= 0 && at < accounts.length - 1, run: () => pane.moveAccountBy(accountMenu.accountId, 1) },
                { text: qsTr("Move to top"), enabled: at > 0, run: () => pane.moveAccountBy(accountMenu.accountId, -at) }
            ];
        }
    }
}
