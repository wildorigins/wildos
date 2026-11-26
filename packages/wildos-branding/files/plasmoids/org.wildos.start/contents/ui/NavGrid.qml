import QtQuick

// A grid that moves with the arrow keys and hands focus to its neighbours
// when the selection runs off the top or bottom edge. With `reorder` its
// items can be dragged to a new place, or moved with Alt+Shift and an
// arrow as on Windows; the grid says where and its owner moves the row.
GridView {
    id: grid

    property int columns: 1
    property int limit: 0
    property int maxRows: 3
    property real gap: 2
    property real rowHeight: 58

    readonly property int shownCount: limit > 0 ? Math.min(count, limit) : count
    readonly property int rows: Math.ceil(shownCount / columns)

    signal exitUp()
    signal exitDown()
    signal activated(int index)
    signal menuRequested(int index, Item item, real x, real y)

    property bool reorder: false
    // While an item is being dragged: where it came from, the place it
    // would take, its icon and where the pointer is in the grid's content.
    property int dragFrom: -1
    property int dragTo: -1
    property var dragIcon: ""
    property point dragAt: Qt.point(0, 0)
    signal moved(int from, int to)

    // With `mergeable`, letting go over the middle of another item drops
    // the dragged one onto it (Start makes a folder that way) rather than
    // beside it. canMerge(from, onto) says which pairs may.
    property bool mergeable: false
    property var canMerge: (from, onto) => true
    property int dragOnto: -1
    signal merged(int from, int onto)
    // Let go outside the grid's visible area (out of an open folder).
    readonly property bool dragOutside: dragFrom >= 0
        && (dragAt.x < 0 || dragAt.x > width || dragAt.y < contentY || dragAt.y > contentY + height)
    signal droppedOutside(int from)

    function dragStart(index: int, icon: var): void {
        dragFrom = index;
        dragTo = index;
        dragOnto = -1;
        dragIcon = icon;
    }

    function dragMove(x: real, y: real): void {
        dragAt = Qt.point(x, y);
        const over = indexAt(x, y);
        if (over < 0 || over >= shownCount) {
            dragTo = dragFrom;
            dragOnto = -1;
            return;
        }
        // The middle half of an item, across and down, is "onto"; nearer
        // its sides is "beside", as before.
        const across = (x - (over % columns) * cellWidth) / cellWidth;
        const down = (y - Math.floor(over / columns) * cellHeight) / cellHeight;
        if (mergeable && over !== dragFrom && across > 0.25 && across < 0.75 && down > 0.15 && down < 0.85
                && canMerge(dragFrom, over)) {
            dragOnto = over;
            dragTo = dragFrom;
        } else {
            dragOnto = -1;
            dragTo = over;
        }
    }

    function dragEnd(drop: bool): void {
        const from = dragFrom;
        const to = dragTo;
        const onto = dragOnto;
        const outside = dragOutside;
        dragFrom = -1;
        dragTo = -1;
        dragOnto = -1;
        dragIcon = "";
        if (!drop || from < 0) {
            return;
        }
        if (onto >= 0) {
            merged(from, onto);
        } else if (outside) {
            droppedOutside(from);
        } else if (to >= 0 && to !== from) {
            moved(from, to);
        }
    }

    // What is being dragged, under the pointer.
    AppIcon {
        visible: grid.dragFrom >= 0
        x: grid.dragAt.x - width / 2
        y: grid.dragAt.y - height / 2
        z: 10
        width: 42
        height: 42
        opacity: 0.85
        source: grid.dragIcon
    }

    // Callers size the view one gap wider than the content so the last
    // column fits; each delegate then leaves the gap on its right.
    cellWidth: width / columns
    cellHeight: rowHeight + gap
    implicitHeight: Math.max(rows, 1) > maxRows ? maxRows * cellHeight - gap : Math.max(rows, 1) * cellHeight - gap
    interactive: rows > maxRows
    clip: rows > maxRows
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    highlightFollowsCurrentItem: false
    currentIndex: -1
    reuseItems: false

    function enter(fromBelow: bool): void {
        if (shownCount === 0) {
            return;
        }
        if (currentIndex < 0 || currentIndex >= shownCount) {
            currentIndex = fromBelow ? (rows - 1) * columns : 0;
        }
        forceActiveFocus(fromBelow ? Qt.BacktabFocusReason : Qt.TabFocusReason);
    }

    onActiveFocusChanged: {
        if (activeFocus && (currentIndex < 0 || currentIndex >= shownCount) && shownCount > 0) {
            currentIndex = 0;
        }
    }

    Keys.onPressed: event => {
        const current = Math.max(currentIndex, 0);
        const column = current % columns;
        const arrows = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down];
        if (reorder && (event.modifiers & Qt.AltModifier) && (event.modifiers & Qt.ShiftModifier) && arrows.indexOf(event.key) >= 0) {
            const to = current + [-1, 1, -columns, columns][arrows.indexOf(event.key)];
            if (currentIndex >= 0 && to >= 0 && to < shownCount) {
                moved(current, to);
                currentIndex = to;
                positionViewAtIndex(to, GridView.Contain);
            }
            Nav.keyboard = true;
            event.accepted = true;
            return;
        }
        switch (event.key) {
        case Qt.Key_Left:
            if (current > 0) {
                currentIndex = current - 1;
            }
            break;
        case Qt.Key_Right:
            if (current < shownCount - 1) {
                currentIndex = current + 1;
            }
            break;
        case Qt.Key_Up:
            if (current - columns >= 0) {
                currentIndex = current - columns;
            } else {
                exitUp();
            }
            break;
        case Qt.Key_Down:
            if (current + columns < shownCount) {
                currentIndex = current + columns;
            } else if (Math.floor(current / columns) < rows - 1) {
                currentIndex = shownCount - 1;
            } else {
                exitDown();
            }
            break;
        case Qt.Key_Home:
            currentIndex = 0;
            break;
        case Qt.Key_End:
            currentIndex = shownCount - 1;
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (currentIndex >= 0) {
                activated(currentIndex);
            }
            break;
        case Qt.Key_Menu:
            if (currentItem) {
                menuRequested(currentIndex, currentItem, currentItem.width / 2, currentItem.height / 2);
            }
            break;
        default:
            if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) && currentItem) {
                menuRequested(currentIndex, currentItem, currentItem.width / 2, currentItem.height / 2);
                break;
            }
            return;
        }
        Nav.keyboard = true;
        positionViewAtIndex(currentIndex, GridView.Contain);
        event.accepted = true;
    }
}
