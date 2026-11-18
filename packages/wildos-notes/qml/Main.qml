import QtCore
import QtQuick
import QtQuick.Layouts
import WildOS.Notes
import WildOS.UI as UI

// Wild OS Notes (AppNotes.dc): the title bar, the sidebar of notebooks and
// tags, the list of notes, and the open note.
UI.AppWindow {
    id: window

    property string selectedPath

    // Named, since a quick note can be the first window Notes makes.
    stateKey: "main"
    width: 1384
    height: 802
    minimumWidth: 900
    minimumHeight: 560
    title: qsTr("Notes")

    // Called by NotesApp when a quick note asks to be opened here.
    function openPath(path) {
        const notebook = NoteStore.notebookOf(path);
        NoteStore.search = "";
        list.searching = false;
        if (NoteStore.indexOfPath(path) < 0 && notebook.length > 0) {
            NoteStore.viewKey = notebook;
            NoteStore.viewKind = "notebook";
        }
        select(path);
    }
    function select(path) {
        if (path.length === 0) {
            doc.close();
        } else if (!doc.load(path)) {
            return;
        }
        selectedPath = doc.path;
    }
    function newNote() {
        let notebook = NoteStore.viewKind === "notebook" ? NoteStore.viewKey : NoteStore.defaultNotebook;
        // A new note has no tags and no pin, so a list that wouldn't show it
        // gives way to its notebook.
        if (NoteStore.search.length > 0 || NoteStore.viewKind === "tag" || NoteStore.viewKind === "pinned") {
            list.stopSearch();
            NoteStore.viewKey = notebook;
            NoteStore.viewKind = "notebook";
        }
        if (doc.create(notebook)) {
            selectedPath = doc.path;
            editor.focusTitle();
        } else {
            say(NoteStore.lastError);
        }
    }
    function deleteNote(path) {
        if (path === doc.path) {
            doc.close();
        }
        if (NoteStore.trashNote(path)) {
            say(qsTr("Moved to the bin"), qsTr("Undo"));
        } else {
            say(NoteStore.lastError);
        }
        ensureSelection();
    }
    function moveNote(path, notebook) {
        const open = path === doc.path;
        if (open) {
            doc.save();
        }
        const moved = NoteStore.moveNote(path, notebook);
        if (moved.length === 0) {
            say(NoteStore.lastError);
            return;
        }
        say(qsTr("Moved to %1").arg(notebook));
        if (open) {
            selectedPath = moved;
        }
        ensureSelection();
    }
    function ensureSelection() {
        if (selectedPath.length > 0 && NoteStore.indexOfPath(selectedPath) >= 0) {
            return;
        }
        if (doc.open && NoteStore.indexOfPath(doc.path) >= 0) {
            selectedPath = doc.path;
            return;
        }
        select(NoteStore.pathAt(0));
    }
    function say(message, action) {
        if (message.length > 0) {
            toast.show(message, action);
        }
    }
    function undoDelete() {
        const path = NoteStore.restoreTrashed();
        if (path.length === 0) {
            say(NoteStore.lastError);
        } else if (NoteStore.indexOfPath(path) >= 0) {
            select(path);
        }
    }

    onClosing: {
        doc.close();
        settings.viewKind = NoteStore.viewKind;
        settings.viewKey = NoteStore.viewKey;
        settings.sort = NoteStore.sort;
        settings.lastNote = selectedPath;
    }
    Component.onCompleted: {
        NoteStore.sort = settings.sort;
        if (settings.viewKind !== "notebook" || NoteStore.notebooks.some(n => n.name === settings.viewKey)) {
            NoteStore.viewKey = settings.viewKey;
            NoteStore.viewKind = settings.viewKind;
        }
        if (settings.lastNote.length > 0 && NoteStore.indexOfPath(settings.lastNote) >= 0) {
            select(settings.lastNote);
        } else {
            select(NoteStore.pathAt(0));
        }
    }

    Settings {
        id: settings
        category: "Window"
        property string viewKind: "all"
        property string viewKey: ""
        property string sort: "edited"
        property string lastNote: ""
    }

    NoteDocument {
        id: doc
        onPathChanged: {
            if (open) {
                window.selectedPath = path;
            }
        }
    }

    Connections {
        target: NoteStore
        function onCountChanged() {
            Qt.callLater(window.ensureSelection);
        }
        function onViewChanged() {
            Qt.callLater(window.ensureSelection);
        }
        function onLibraryChanged() {
            if (doc.open && !NoteStore.exists(doc.path)) {
                // Deleted or moved away outside Notes.
                doc.close();
                window.selectedPath = "";
            }
            Qt.callLater(window.ensureSelection);
        }
    }

    Shortcut {
        sequence: "Ctrl+N"
        onActivated: window.newNote()
    }
    // Ctrl+F looks through the note being written in, and through every
    // note from anywhere else; Ctrl+Shift+F is always every note.
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: editor.editing ? editor.startFind() : list.startSearch()
    }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        onActivated: list.startSearch()
    }
    Shortcut {
        sequence: StandardKey.Print
        enabled: doc.open
        onActivated: editor.print()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            tall: true
            frame: window.frame
            showTitle: false

            // The title bar puts its content 16px in; the board's icon is 14.
            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: "qrc:/qt/qml/WildOS/Notes/data/wildos-notes.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Notes")
                    font.weight: UI.Style.weightMedium
                    color: window.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                onNewNotebook: nameDialog.ask("")
                onNotebookMenu: (name, row, x, y) => {
                    notebookMenu.notebook = name;
                    notebookMenu.popup(row, x, y);
                }
            }

            NoteList {
                id: list
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                selectedPath: window.selectedPath
                onSelected: path => window.select(path)
                onNewNote: window.newNote()
                onDeleteRequested: path => window.deleteNote(path)
                onNoteMenu: (path, row, x, y) => {
                    noteMenu.path = path;
                    noteMenu.popup(row, x, y);
                }
            }

            EditorPane {
                id: editor
                Layout.fillWidth: true
                Layout.fillHeight: true
                doc: doc
                onShowNotebook: name => {
                    list.stopSearch();
                    NoteStore.viewKey = name;
                    NoteStore.viewKind = "notebook";
                }
                onShowTag: tag => {
                    list.stopSearch();
                    NoteStore.viewKey = tag;
                    NoteStore.viewKind = "tag";
                }
                onMoveRequested: (path, notebook) => window.moveNote(path, notebook)
                onDeleteRequested: path => window.deleteNote(path)
                onSaid: message => window.say(message)
            }
        }
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        // Later, because the toast hides itself once this returns and
        // would take a message saying why it didn't work with it.
        onActionClicked: Qt.callLater(window.undoDelete)
    }

    UI.Menu {
        id: noteMenu
        // A right click opens the note first, so the menu acts on the open
        // note.
        property string path
        entries: {
            const pinned = doc.pinned;
            return [
                { text: pinned ? qsTr("Unpin") : qsTr("Pin"), icon: pinned ? "unpin" : "pin", run: () => doc.pinned = !doc.pinned },
                { text: qsTr("Colour"), icon: "palette", submenu: [
                    { text: qsTr("None"), radio: true, checked: !NoteColours.known(doc.colour), run: () => doc.colour = "" }
                ].concat(NoteColours.all.map(c => ({ text: c.name, checked: doc.colour === c.id, radio: true, run: () => doc.colour = c.id }))) },
                { text: qsTr("Move to"), icon: "folder", submenu: NoteStore.notebooks.map(n => ({
                    text: n.name, enabled: n.name !== NoteStore.notebookOf(noteMenu.path), run: () => window.moveNote(noteMenu.path, n.name)
                })) },
                { text: qsTr("Show in Files"), icon: "open-folder", run: () => NoteStore.showInFiles(noteMenu.path) },
                { separator: true },
                { text: qsTr("Delete"), icon: "delete", danger: true, run: () => window.deleteNote(noteMenu.path) }
            ];
        }
    }

    UI.Menu {
        id: notebookMenu
        property string notebook
        entries: [
            { text: qsTr("Rename"), icon: "rename", run: () => nameDialog.ask(notebookMenu.notebook) },
            { text: qsTr("Show in Files"), icon: "open-folder", run: () => NoteStore.showInFiles(NoteStore.notebookDir(notebookMenu.notebook)) },
            { separator: true },
            { text: qsTr("Delete"), icon: "delete", danger: true, run: () => deleteDialog.ask(notebookMenu.notebook) }
        ]
    }

    // New notebook, and Rename.
    UI.Dialog {
        id: nameDialog
        property string renaming
        function ask(name) {
            renaming = name;
            nameField.text = name;
            nameField.error = false;
            open();
            nameField.forceActiveFocus();
            nameField.selectAll();
        }
        function confirm() {
            if (renaming.length > 0) {
                const name = NoteStore.renameNotebook(renaming, nameField.text);
                if (name.length === 0) {
                    nameField.error = true;
                    window.say(NoteStore.lastError);
                    return;
                }
            } else {
                const name = NoteStore.createNotebook(nameField.text);
                if (name.length === 0) {
                    nameField.error = true;
                    window.say(NoteStore.lastError);
                    return;
                }
                NoteStore.viewKey = name;
                NoteStore.viewKind = "notebook";
            }
            close();
        }
        title: renaming.length > 0 ? qsTr("Rename notebook") : qsTr("New notebook")
        text: renaming.length > 0 ? qsTr("The folder in Notes is renamed too.") : qsTr("A notebook is a folder in Notes in your home folder.")

        UI.TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
        }

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: nameDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: nameDialog.renaming.length > 0 ? qsTr("Rename") : qsTr("Create")
                onClicked: nameDialog.confirm()
            }
        ]
    }

    UI.Dialog {
        id: deleteDialog
        property string notebook
        function ask(name) {
            notebook = name;
            open();
        }
        title: qsTr("Delete %1?").arg(notebook)
        text: {
            const count = NoteStore.notebookCount(notebook);
            return count === 0 ? qsTr("The notebook goes to the bin.")
                : count === 1 ? qsTr("The notebook and its note go to the bin, where you can get them back.")
                : qsTr("The notebook and its %1 notes go to the bin, where you can get them back.").arg(count);
        }

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: deleteDialog.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Delete")
                onClicked: {
                    const name = deleteDialog.notebook;
                    deleteDialog.close();
                    if (doc.open && NoteStore.notebookOf(doc.path) === name) {
                        doc.close();
                    }
                    if (NoteStore.trashNotebook(name)) {
                        window.say(qsTr("%1 moved to the bin").arg(name), qsTr("Undo"));
                    } else {
                        window.say(NoteStore.lastError);
                    }
                }
            }
        ]
    }
}
