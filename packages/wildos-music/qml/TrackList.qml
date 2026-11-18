import QtQuick
import QtQuick.Templates as T
import WildOS.Music
import WildOS.UI as UI

// A page that is mostly songs: whatever the page puts on top (an album's
// cover and buttons, a heading), the column names, then the rows, all
// scrolling together. Rows sit 14px in from the page and the column names
// 28px, as on the board.
ListView {
    id: list

    property var tracks: []
    // "track" numbers rows by their track number, "index" counts them.
    property string numbering: "index"
    property bool showAlbum: false
    property Component heading
    property bool showColumns: tracks.length > 0
    property Component empty

    signal playAt(int index)
    signal menuAt(int index, Item row, real x, real y)
    // Delete was pressed with songs selected; picked() says which.
    signal deleteRequested()

    // The selected rows, as { index: true }, the row a Shift range runs
    // from, and the row the keyboard is on.
    property var selection: ({})
    property int anchor: -1
    property int cursor: -1
    readonly property int selectedCount: Object.keys(selection).length

    function isSelected(index) {
        return selection[index] === true;
    }
    // As in Files: a plain click picks one row, Ctrl adds or drops it, and
    // Shift takes every row from the last one picked.
    function select(index, modifiers) {
        const next = {};
        if ((modifiers & Qt.ShiftModifier) && anchor >= 0) {
            if (modifiers & Qt.ControlModifier) {
                Object.assign(next, selection);
            }
            for (let i = Math.min(anchor, index); i <= Math.max(anchor, index); ++i) {
                next[i] = true;
            }
        } else if (modifiers & Qt.ControlModifier) {
            Object.assign(next, selection);
            if (next[index]) {
                delete next[index];
            } else {
                next[index] = true;
            }
            anchor = index;
        } else {
            next[index] = true;
            anchor = index;
        }
        selection = next;
        cursor = index;
    }
    function selectAll() {
        const next = {};
        for (let i = 0; i < tracks.length; ++i) {
            next[i] = true;
        }
        selection = next;
    }
    function clearSelection() {
        selection = ({});
        anchor = -1;
    }
    // The selected songs, in the order the list shows them.
    function picked() {
        return Object.keys(selection).map(Number).filter(i => i < tracks.length).sort((a, b) => a - b).map(i => tracks[i]);
    }
    function step(by, modifiers) {
        if (count === 0) {
            return;
        }
        const to = Math.max(0, Math.min(count - 1, cursor + by));
        select(to, modifiers & Qt.ShiftModifier);
        positionViewAtIndex(to, ListView.Contain);
    }

    onTracksChanged: {
        clearSelection();
        cursor = -1;
    }

    readonly property real artistWidth: Math.min(200, Math.max(110, (width - 28 - 200) * 0.22))

    clip: true
    focus: true
    model: tracks
    currentIndex: -1
    boundsBehavior: Flickable.StopAtBounds
    T.ScrollBar.vertical: UI.ScrollBar {}
    bottomMargin: 16
    keyNavigationEnabled: false
    activeFocusOnTab: true
    Accessible.role: Accessible.Table

    Keys.onUpPressed: event => step(-1, event.modifiers)
    Keys.onDownPressed: event => step(1, event.modifiers)
    Keys.onReturnPressed: if (cursor >= 0) list.playAt(cursor)
    Keys.onEnterPressed: if (cursor >= 0) list.playAt(cursor)
    Keys.onDeletePressed: if (selectedCount > 0) list.deleteRequested()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            selectAll();
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape && selectedCount > 0) {
            clearSelection();
            event.accepted = true;
        } else if ((event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) && cursor >= 0) {
            const row = itemAtIndex(cursor);
            if (row) {
                list.menuAt(cursor, row, 64, row.height - 6);
            }
            event.accepted = true;
        }
    }

    header: Column {
        width: list.width

        Loader {
            width: parent.width
            sourceComponent: list.heading
        }
        TrackHeader {
            visible: list.showColumns
            x: 28
            width: parent.width - 56
            showAlbum: list.showAlbum
            artistWidth: list.artistWidth
        }
        Item {
            width: 1
            height: 6
        }
        Loader {
            width: parent.width
            height: item ? item.implicitHeight : 0
            active: list.tracks.length === 0 && list.empty !== null
            sourceComponent: list.empty
        }
    }

    // ListView places its delegates at x 0, so the row is inset in a
    // full-width holder.
    delegate: Item {
        id: holder
        required property var modelData
        required property int index
        width: list.width
        height: trackRow.height

        TrackRow {
            id: trackRow
            x: 14
            width: list.width - 28
            track: holder.modelData
            number: list.numbering === "track" ? (holder.modelData.number ?? 0) : holder.index + 1
            showAlbum: list.showAlbum
            artistWidth: list.artistWidth
            selected: list.selection[holder.index] === true
            onPressed: (button, modifiers) => {
                list.forceActiveFocus();
                // A right click on a selected song keeps the others
                // selected, for the menu to act on them all.
                if (button !== Qt.RightButton) {
                    list.select(holder.index, modifiers);
                } else if (!list.isSelected(holder.index)) {
                    list.select(holder.index, Qt.NoModifier);
                } else {
                    list.cursor = holder.index;
                }
            }
            onPlay: list.playAt(holder.index)
            onMenuRequested: (x, y) => list.menuAt(holder.index, trackRow, x, y)
        }
    }
}
