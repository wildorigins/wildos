pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import "code/helpers.js" as Helpers
import WildOS.UI as UI

// Every installed app, A to Z by the name Start shows, grouped under its
// first letter. Kicker sorts by the desktop file name, which would file
// "Files" under D for Dolphin, so the list is sorted again here. An accented
// first letter goes with its plain one, and other alphabets get letters of
// their own after Z. Choosing a letter heading (or Left or Right in the
// list) shows every letter to jump to, as on Windows.
FocusScope {
    id: page

    required property var launcher

    property alias backButton: header.button
    property alias list: list

    signal backRequested()
    signal leaveUp()
    signal leaveDown()
    signal menuRequested(var model, int index, Item item, real x, real y, bool pinned)

    // The letters to jump to: { letter, row }, row -1 with no apps there.
    property var letters: []
    property bool picking: false

    function letterOf(name: string): string {
        const base = name.trim().charAt(0).normalize("NFD").toUpperCase().charAt(0);
        return base.length > 0 && base.toLowerCase() !== base ? base : "#";
    }

    function showLetters(letter: string): void {
        letterGrid.currentIndex = Math.max(0, letters.findIndex(entry => entry.letter === letter));
        picking = true;
        if (Nav.keyboard) {
            letterGrid.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    function jumpTo(row: int): void {
        if (row < 0) {
            return;
        }
        picking = false;
        list.currentIndex = row;
        list.positionViewAtIndex(row, ListView.Beginning);
        if (Nav.keyboard) {
            list.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    function enter(fromBelow: bool): void {
        if (picking) {
            letterGrid.enter(fromBelow);
            return;
        }
        if (list.count === 0) {
            return;
        }
        if (list.currentIndex < 0) {
            list.currentIndex = 0;
        }
        list.forceActiveFocus(fromBelow ? Qt.BacktabFocusReason : Qt.TabFocusReason);
    }

    function rebuild(): void {
        const items = [];
        for (let i = 0; i < apps.count; i++) {
            const entry = apps.objectAt(i);
            if (!entry || entry.model.isSeparator || entry.model.hasChildren) {
                continue;
            }
            const name = Helpers.appName(entry.model.favoriteId, entry.model.display);
            items.push({ name: name, row: i, letter: letterOf(name) });
        }
        // By letter first, so each letter's apps stay together however the
        // language orders accents.
        items.sort((a, b) => a.letter === b.letter ? a.name.localeCompare(b.name)
            : a.letter === "#" ? -1 : b.letter === "#" ? 1 : a.letter.localeCompare(b.letter));
        sorted.clear();
        const firstRow = {};
        const all = ["#"].concat("ABCDEFGHIJKLMNOPQRSTUVWXYZ".split(""));
        for (let i = 0; i < items.length; i++) {
            const letter = items[i].letter;
            sorted.append(items[i]);
            if (firstRow[letter] === undefined) {
                firstRow[letter] = i;
            }
            if (all.indexOf(letter) < 0) {
                all.push(letter);
            }
        }
        letters = all.map(letter => ({ letter: letter, row: firstRow[letter] !== undefined ? firstRow[letter] : -1 }));
    }

    Instantiator {
        id: apps
        model: page.launcher.appsModel
        delegate: QtObject {
            required property var model
            required property int index
        }
        onObjectAdded: rebuildTimer.restart()
        onObjectRemoved: rebuildTimer.restart()
    }

    Timer {
        id: rebuildTimer
        interval: 0
        onTriggered: page.rebuild()
    }

    ListModel {
        id: sorted
    }

    SectionHeader {
        id: header
        width: parent.width
        title: qsTr("All apps")
        buttonText: qsTr("Back")
        backward: true
        onActivated: page.backRequested()
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !page.picking
        clip: true
        model: sorted
        currentIndex: -1
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        boundsBehavior: Flickable.StopAtBounds
        spacing: 2

        T.ScrollBar.vertical: UI.ScrollBar {}

        section.property: "letter"
        section.criteria: ViewSection.FullString
        section.delegate: Item {
            id: heading

            required property string section
            width: ListView.view.width
            height: 30

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("%1, jump to a letter").arg(section)

            Rectangle {
                y: 2
                width: Math.max(28, letterLabel.implicitWidth + 16)
                height: 24
                radius: 6
                color: headingMouse.pressed ? UI.Style.surfacePressed : headingMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                MouseArea {
                    id: headingMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: page.showLetters(heading.section)
                }
            }

            UI.Label {
                id: letterLabel
                x: 8
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                size: 12
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkMuted
                text: heading.section
            }
        }

        onActiveFocusChanged: {
            if (activeFocus && currentIndex < 0 && count > 0) {
                currentIndex = 0;
            }
        }

        function activate(index: int): void {
            const row = sorted.get(index).row;
            if (page.launcher.appsModel.trigger(row, "", null)) {
                page.launcher.close();
            }
        }

        function requestMenu(index: int, item: Item, x: real, y: real): void {
            page.menuRequested(page.launcher.appsModel, sorted.get(index).row, item, x, y, false);
        }

        Keys.onPressed: event => {
            switch (event.key) {
            case Qt.Key_Up:
                if (currentIndex > 0) {
                    currentIndex--;
                } else {
                    page.leaveUp();
                }
                break;
            case Qt.Key_Down:
                if (currentIndex < count - 1) {
                    currentIndex++;
                } else {
                    page.leaveDown();
                }
                break;
            case Qt.Key_PageUp:
                currentIndex = Math.max(0, currentIndex - 8);
                break;
            case Qt.Key_PageDown:
                currentIndex = Math.min(count - 1, currentIndex + 8);
                break;
            case Qt.Key_Home:
                currentIndex = 0;
                break;
            case Qt.Key_End:
                currentIndex = count - 1;
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                activate(currentIndex);
                break;
            case Qt.Key_Menu:
                if (currentItem) {
                    requestMenu(currentIndex, currentItem, currentItem.width / 2, currentItem.height / 2);
                }
                break;
            case Qt.Key_Left:
            case Qt.Key_Right:
                Nav.keyboard = true;
                page.showLetters(currentIndex >= 0 ? sorted.get(currentIndex).letter : "#");
                event.accepted = true;
                return;
            default:
                return;
            }
            Nav.keyboard = true;
            positionViewAtIndex(currentIndex, ListView.Contain);
            event.accepted = true;
        }

        delegate: Item {
            id: appRow

            required property int index
            required property string name
            required property int row

            readonly property var app: apps.objectAt(row) ? apps.objectAt(row).model : null

            width: ListView.view.width - 12
            height: 44

            Accessible.role: Accessible.Button
            Accessible.name: name

            Rectangle {
                anchors.fill: parent
                radius: Style.radiusItem
                color: mouse.pressed ? UI.Style.surfacePressed : mouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
            }

            AppIcon {
                id: icon
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                source: appRow.app ? appRow.app.decoration : ""
            }

            UI.Label {
                anchors.left: icon.right
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                font.weight: UI.Style.weightMedium
                text: appRow.name
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: list.currentIndex = appRow.index
                onClicked: event => {
                    if (event.button === Qt.RightButton) {
                        list.requestMenu(appRow.index, appRow, event.x, event.y);
                    } else {
                        list.activate(appRow.index);
                    }
                }
                onPressAndHold: event => list.requestMenu(appRow.index, appRow, event.x, event.y)
            }

            UI.FocusRing {
                anchors.margins: -2
                radius: Style.radiusItem + 2
                visible: appRow.ListView.isCurrentItem && list.activeFocus && Nav.keyboard
            }
        }
    }

    NavGrid {
        id: letterGrid
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: -gap
        height: Math.min(implicitHeight, parent.height - header.height - 8)
        visible: page.picking
        columns: 7
        rowHeight: 48
        maxRows: Math.max(1, Math.floor((parent.height - header.height - 8 + gap) / cellHeight))
        model: page.letters
        onActivated: index => page.jumpTo(page.letters[index].row)
        onExitUp: page.leaveUp()
        onExitDown: page.leaveDown()

        delegate: Item {
            id: cell

            required property var modelData
            required property int index
            readonly property bool used: modelData.row >= 0

            width: letterGrid.cellWidth - letterGrid.gap
            height: letterGrid.rowHeight

            Accessible.role: Accessible.Button
            Accessible.name: modelData.letter
            Accessible.onPressAction: page.jumpTo(modelData.row)

            Rectangle {
                anchors.fill: parent
                radius: Style.radiusItem
                color: !cell.used ? "transparent"
                    : cellMouse.pressed ? UI.Style.surfacePressed
                    : cellMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceRest
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
            }

            UI.Label {
                anchors.centerIn: parent
                size: 14
                font.weight: UI.Style.weightMedium
                color: cell.used ? UI.Style.ink : UI.Style.inkDisabled
                text: cell.modelData.letter
            }

            MouseArea {
                id: cellMouse
                anchors.fill: parent
                enabled: cell.used
                hoverEnabled: true
                onPressed: letterGrid.currentIndex = cell.index
                onClicked: page.jumpTo(cell.modelData.row)
            }

            UI.FocusRing {
                visible: cell.GridView.isCurrentItem && letterGrid.activeFocus && Nav.keyboard
                targetRadius: Style.radiusItem
            }
        }
    }
}
