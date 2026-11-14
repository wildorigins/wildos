import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import WildOS.Notes
import WildOS.UI as UI

// The middle column (AppNotes.dc): what the sidebar picked as a heading,
// with search and sort, the accent New note button, then the notes.
// Search looks through every note, titles and text.
Item {
    id: list

    property string selectedPath
    property bool searching: false

    signal selected(string path)
    signal newNote()
    signal noteMenu(string path, Item row, real x, real y)

    readonly property string heading: {
        if (NoteStore.viewKind === "pinned") {
            return qsTr("Pinned");
        }
        if (NoteStore.viewKind === "notebook") {
            return NoteStore.viewKey;
        }
        if (NoteStore.viewKind === "tag") {
            return "#" + NoteStore.viewKey;
        }
        return qsTr("All notes");
    }

    function startSearch() {
        searching = true;
        searchField.forceActiveFocus();
        searchField.selectAll();
    }
    function stopSearch() {
        searching = false;
        NoteStore.search = "";
    }
    function step(by) {
        const row = NoteStore.indexOfPath(selectedPath) + by;
        if (row >= 0 && row < NoteStore.count) {
            list.selected(NoteStore.pathAt(row));
            notes.positionViewAtIndex(row, ListView.Contain);
        }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: UI.Style.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 58

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 14
                anchors.bottomMargin: 10
                spacing: 8

                UI.Label {
                    visible: !list.searching
                    Layout.fillWidth: true
                    text: list.heading
                    size: 16
                    font.weight: UI.Style.weightBold
                    Accessible.role: Accessible.Heading
                }
                UI.SearchField {
                    id: searchField
                    visible: list.searching
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    placeholderText: qsTr("Search notes")
                    onTextChanged: NoteStore.search = text
                    onEscapePressed: list.stopSearch()
                    onDownPressed: notes.forceActiveFocus()
                    // An empty box closes when you leave it, but not for the text
                    // menu, which holds focus while it is up and may be there to
                    // paste.
                    onActiveFocusChanged: {
                        if (!activeFocus && !textMenuOpen && text.length === 0) {
                            list.searching = false;
                        }
                    }
                    onTextMenuFocusLeft: {
                        if (text.length === 0) {
                            list.searching = false;
                        }
                    }
                }
                ToolButton {
                    visible: !list.searching
                    iconName: "search"
                    tip: qsTr("Search")
                    onClicked: list.startSearch()
                }
                ToolButton {
                    id: sortButton
                    iconName: "sort"
                    tip: qsTr("Sort")
                    onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4)

                    UI.Menu {
                        id: sortMenu
                        entries: [
                            { header: qsTr("Sort by") },
                            { text: qsTr("Date edited"), radio: true, checked: NoteStore.sort === "edited", run: () => NoteStore.sort = "edited" },
                            { text: qsTr("Date created"), radio: true, checked: NoteStore.sort === "created", run: () => NoteStore.sort = "created" },
                            { text: qsTr("Title"), radio: true, checked: NoteStore.sort === "title", run: () => NoteStore.sort = "title" }
                        ]
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 42

            NewNoteButton {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.top: parent.top
                onClicked: list.newNote()
            }
        }

        ListView {
            id: notes
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: NoteStore
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            activeFocusOnTab: true
            T.ScrollBar.vertical: UI.ScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: list.heading

            delegate: NoteRow {
                id: noteRow
                width: notes.width
                current: path === list.selectedPath
                onSelected: p => list.selected(p)
                onMenuRequested: (p, x, y) => list.noteMenu(p, noteRow, x, y)
            }

            Keys.onUpPressed: list.step(-1)
            Keys.onDownPressed: list.step(1)

            UI.Label {
                visible: notes.count === 0
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: NoteStore.search.length > 0 ? qsTr("No notes match %1").arg(NoteStore.search)
                    : NoteStore.viewKind === "pinned" ? qsTr("Pin a note to keep it here, at the top of every list.")
                    : qsTr("No notes here yet.")
                size: 13
                color: UI.Style.inkSubtle
            }
        }
    }
}
