import QtCore
import QtQuick
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// Wild OS Music (AppMusic.dc): the title bar, the sidebar, the page it
// picks, the queue or lyrics when they are open, and the player bar along
// the foot of the window.
UI.AppWindow {
    id: window

    // What the page shows: "home", "search", "songs", "albums", "album",
    // "artists", "artist", "folders", "liked" or "playlist", and which one.
    property string kind: "home"
    property string key
    property var history: []
    property string panel
    property string searchText
    // Songs waiting for a playlist from Add to playlist > New playlist.
    property var pendingSongs: []
    // What the button on the note at the foot of the window does.
    property var toastAction: null
    // The keyboard is in a text box: single keys belong to it.
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    // The first walk of the Music folder has found nothing to show yet.
    // Playlists are files of their own and can be shown meanwhile.
    readonly property bool looking: (!Library.ready || (Library.scanning && Library.songCount === 0)) && kind !== "playlist" && kind !== "liked"

    width: 1384
    height: 802
    minimumWidth: 960
    minimumHeight: 600
    title: qsTr("Music")

    function navigate(newKind, newKey) {
        newKey = newKey ?? "";
        if (newKind === kind && newKey === key) {
            if (newKind === "search" && pageLoader.item) {
                pageLoader.item.focusField();
            }
            return;
        }
        history = history.concat([{ kind: kind, key: key }]).slice(-50);
        kind = newKind;
        key = newKey;
    }
    function back() {
        if (history.length === 0) {
            return;
        }
        const last = history[history.length - 1];
        history = history.slice(0, -1);
        kind = last.kind;
        key = last.key;
    }
    function play(paths, index, shuffled) {
        Player.playSongs(paths, index, shuffled);
    }
    function say(message, action, run) {
        if (message.length > 0) {
            toastAction = run ?? null;
            toast.show(message, action);
        }
    }
    function addToPlaylist(name, paths) {
        if (Playlists.add(name, paths)) {
            say(paths.length === 1 ? qsTr("Added to %1").arg(name) : qsTr("%1 added to %2").arg(MusicStyle.songs(paths.length)).arg(name));
        } else {
            say(Playlists.lastError);
        }
    }
    function removeFromPlaylist(playlist, paths) {
        if (paths.length === 0) {
            return;
        }
        const before = Playlists.songs(playlist);
        for (const path of paths) {
            Playlists.removeSong(playlist, path);
        }
        const name = playlist === "Liked songs" ? qsTr("Liked songs") : playlist;
        say(paths.length === 1 ? qsTr("Removed from %1").arg(name) : qsTr("%1 removed from %2").arg(MusicStyle.songs(paths.length)).arg(name),
            qsTr("Undo"), () => {
                if (!Playlists.setSongs(playlist, before)) {
                    say(Playlists.lastError);
                }
            });
    }
    function playlistEntries(paths) {
        const entries = [
            { text: qsTr("New playlist"), icon: "plus", run: () => nameDialog.ask("", paths) },
            { separator: true },
            { text: qsTr("Liked songs"), icon: "star", run: () => {
                Playlists.setLikedAll(paths, true);
                say(qsTr("Added to Liked songs"));
            } }
        ];
        for (const p of Playlists.all) {
            entries.push({ text: p.name, icon: "list", run: () => addToPlaylist(p.name, paths) });
        }
        return entries;
    }
    function trackMenu(track, playlist, item, x, y) {
        const paths = [track.path];
        const liked = Playlists.isLiked(track.path);
        const entries = [
            { text: qsTr("Play next"), icon: "play", enabled: !track.missing, run: () => Player.playNext(paths) },
            { text: qsTr("Add to queue"), icon: "list", enabled: !track.missing, run: () => Player.addToQueue(paths) },
            { text: qsTr("Add to playlist"), icon: "plus", enabled: !track.missing, submenu: playlistEntries(paths) },
            { text: liked ? qsTr("Remove from Liked songs") : qsTr("Add to Liked songs"), icon: "star", enabled: !track.missing, run: () => Playlists.setLiked(track.path, !liked) },
            { separator: true }
        ];
        if ((track.albumKey ?? "").length > 0) {
            entries.push({ text: qsTr("Go to album"), icon: "music", run: () => navigate("album", track.albumKey) });
        }
        if (!track.missing) {
            entries.push({ text: qsTr("Go to artist"), icon: "account", run: () => navigate("artist", track.albumArtist || track.artist) });
            entries.push({ text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(track.path) });
        }
        if (playlist.length > 0) {
            entries.push({ separator: true });
            entries.push({ text: playlist === "Liked songs" ? qsTr("Remove from Liked songs") : qsTr("Remove from this playlist"), icon: "delete", danger: true,
                run: () => removeFromPlaylist(playlist, [track.path]) });
        }
        itemMenu.entries = entries;
        itemMenu.popup(item, x, y);
    }
    // The same for several selected songs at once.
    function tracksMenu(tracks, playlist, item, x, y) {
        const paths = tracks.filter(t => !t.missing).map(t => t.path);
        const entries = [
            { text: qsTr("Play"), icon: "play", enabled: paths.length > 0, run: () => play(paths, 0, false) },
            { text: qsTr("Play next"), icon: "play", enabled: paths.length > 0, run: () => Player.playNext(paths) },
            { text: qsTr("Add to queue"), icon: "list", enabled: paths.length > 0, run: () => Player.addToQueue(paths) },
            { text: qsTr("Add to playlist"), icon: "plus", enabled: paths.length > 0, submenu: playlistEntries(paths) }
        ];
        if (playlist.length > 0) {
            entries.push({ separator: true });
            entries.push({ text: playlist === "Liked songs" ? qsTr("Remove from Liked songs") : qsTr("Remove from this playlist"), icon: "delete", danger: true,
                run: () => removeFromPlaylist(playlist, tracks.map(t => t.path)) });
        }
        itemMenu.entries = entries;
        itemMenu.popup(item, x, y);
    }
    // The ... beside an album, artist or playlist, and a right click on a
    // cover or a folder.
    function collectionMenu(what, which, paths, item, x, y) {
        const entries = [
            { text: qsTr("Play"), icon: "play", run: () => play(paths, 0, false) },
            { text: qsTr("Shuffle"), icon: "refresh", run: () => play(paths, -1, true) },
            { text: qsTr("Play next"), icon: "play", run: () => Player.playNext(paths) },
            { text: qsTr("Add to queue"), icon: "list", run: () => Player.addToQueue(paths) },
            { text: qsTr("Add to playlist"), icon: "plus", submenu: playlistEntries(paths) },
            { separator: true }
        ];
        if (what === "playlist") {
            entries.push({ text: qsTr("Rename"), icon: "rename", run: () => nameDialog.ask(which, []) });
            entries.push({ text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(Playlists.fileOf(which)) });
            entries.push({ separator: true });
            entries.push({ text: qsTr("Delete"), icon: "delete", danger: true, run: () => deleteDialog.ask(which) });
        } else if (what === "liked") {
            entries.push({ text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(Playlists.fileOf("Liked songs")) });
        } else if (what === "folder") {
            entries.push({ text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(Library.musicFolder + "/" + which) });
        } else if (paths.length > 0) {
            if (what === "album") {
                entries.push({ text: qsTr("Go to artist"), icon: "account", run: () => navigate("artist", Library.album(which).artist) });
            }
            entries.push({ text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(paths[0]) });
        }
        if (entries[entries.length - 1].separator) {
            entries.pop();
        }
        itemMenu.entries = entries;
        if (x === undefined) {
            itemMenu.popup(item, 0, item.height + 4);
        } else {
            itemMenu.popup(item, x, y);
        }
    }

    onClosing: {
        settings.kind = kind === "search" ? "home" : kind;
        settings.key = key;
        Player.saveState();
    }
    Component.onCompleted: {
        if (settings.kind !== "playlist" || Playlists.exists(settings.key)) {
            kind = settings.kind;
            key = settings.key;
        }
    }

    Settings {
        id: settings
        category: "Window"
        property string kind: "home"
        property string key: ""
        property string albumSort: "title"
        property string songSort: "title"
    }

    Connections {
        target: Player
        function onFailed(message) {
            window.say(message);
        }
    }
    Connections {
        target: toast
        function onActionClicked() {
            const run = window.toastAction;
            window.toastAction = null;
            if (run) {
                run();
            }
        }
    }
    Connections {
        target: Playlists
        function onChanged() {
            // A playlist renamed or deleted outside Music.
            if (window.kind === "playlist" && !Playlists.exists(window.key)) {
                window.kind = "home";
                window.key = "";
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: window.navigate("search", "")
    }
    Shortcut {
        sequence: "Space"
        enabled: !window.typing
        onActivated: Player.playPause()
    }
    Shortcut {
        sequence: "Shift+Right"
        enabled: !window.typing
        onActivated: Player.skip(10000)
    }
    Shortcut {
        sequence: "Shift+Left"
        enabled: !window.typing
        onActivated: Player.skip(-10000)
    }
    Shortcut {
        sequence: "Ctrl+Up"
        onActivated: Player.stepVolume(1)
    }
    Shortcut {
        sequence: "Ctrl+Down"
        onActivated: Player.stepVolume(-1)
    }
    Shortcut {
        sequence: "M"
        enabled: !window.typing
        onActivated: Player.muted = !Player.muted
    }
    Shortcut {
        sequence: "Ctrl+Right"
        onActivated: Player.next()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        onActivated: Player.previous()
    }
    Shortcut {
        sequences: [StandardKey.Back]
        onActivated: window.back()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.BackButton
        onClicked: window.back()
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (drop.hasUrls && drop.urls.length > 0) {
                const paths = [];
                for (let i = 0; i < drop.urls.length; ++i) {
                    paths.push(drop.urls[i].toString());
                }
                Player.openFiles(paths);
                drop.accept();
            }
        }
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
                    source: "qrc:/qt/qml/WildOS/Music/data/wildos-music.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Music")
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
                Layout.preferredWidth: 232
                Layout.fillHeight: true
                kind: window.kind
                key: window.key
                onNavigate: (k, which) => window.navigate(k, which)
                onNewPlaylist: nameDialog.ask("", [])
                onPlaylistMenu: (name, row, x, y) => window.collectionMenu("playlist", name, Playlists.songs(name), row, x, y)
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Loader {
                    id: pageLoader
                    anchors.fill: parent
                    focus: true
                    visible: !window.looking
                    sourceComponent: {
                        switch (window.kind) {
                        case "search": return searchPage;
                        case "songs": return collectionPage;
                        case "album": return collectionPage;
                        case "artist": return collectionPage;
                        case "playlist": return collectionPage;
                        case "liked": return collectionPage;
                        case "albums": return albumsPage;
                        case "artists": return artistsPage;
                        case "folders": return foldersPage;
                        default: return homePage;
                        }
                    }
                }

                ColumnLayout {
                    visible: window.looking
                    anchors.centerIn: parent
                    width: Math.min(460, parent.width - 56)
                    spacing: 14

                    UI.Spinner {
                        Layout.alignment: Qt.AlignHCenter
                        size: 28
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Looking through your Music folder")
                        size: 14
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }
            }

            SidePanel {
                visible: window.panel.length > 0
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                mode: window.panel
                app: window
                onCloseRequested: window.panel = ""
            }
        }

        PlayerBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            panel: window.panel
            onOpenAlbum: key => window.navigate("album", key)
            onOpenArtist: name => window.navigate("artist", name)
            onPanelRequested: name => window.panel = window.panel === name ? "" : name
        }
    }

    Connections {
        target: Player
        function onCurrentChanged() {
            if (window.panel === "lyrics" && Player.current.lyrics !== true) {
                window.panel = "";
            }
        }
    }

    Component {
        id: homePage
        HomePage {
            app: window
        }
    }
    Component {
        id: searchPage
        SearchPage {
            app: window
            text: window.searchText
            onTextChanged: window.searchText = text
        }
    }
    Component {
        id: collectionPage
        CollectionPage {
            app: window
            kind: window.kind
            key: window.key
            sort: settings.songSort
            onSortChanged: settings.songSort = sort
        }
    }
    Component {
        id: albumsPage
        AlbumsPage {
            app: window
            sort: settings.albumSort
            onSortChanged: settings.albumSort = sort
        }
    }
    Component {
        id: artistsPage
        ArtistsPage {
            app: window
        }
    }
    Component {
        id: foldersPage
        FoldersPage {
            app: window
        }
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 84 + 20
    }

    UI.Menu {
        id: itemMenu
    }

    // New playlist, and Rename.
    UI.Dialog {
        id: nameDialog
        property string renaming
        property var songs: []
        function ask(name, paths) {
            renaming = name;
            songs = paths;
            nameField.text = name.length > 0 ? name : Playlists.freeName(qsTr("New playlist"));
            nameField.error = false;
            open();
            nameField.forceActiveFocus();
            nameField.selectAll();
        }
        function confirm() {
            if (renaming.length > 0) {
                const name = nameField.text.trim();
                if (!Playlists.rename(renaming, name)) {
                    nameField.error = true;
                    window.say(Playlists.lastError);
                    return;
                }
                if (window.kind === "playlist" && window.key === renaming) {
                    window.key = name;
                }
            } else {
                const name = nameField.text.trim();
                if (!Playlists.create(name, songs)) {
                    nameField.error = true;
                    window.say(Playlists.lastError);
                    return;
                }
                if (songs.length > 0) {
                    window.say(songs.length === 1 ? qsTr("Added to %1").arg(name) : qsTr("%1 added to %2").arg(MusicStyle.songs(songs.length)).arg(name));
                } else {
                    window.navigate("playlist", name);
                }
            }
            close();
        }
        title: renaming.length > 0 ? qsTr("Rename playlist") : qsTr("New playlist")
        text: renaming.length > 0 ? qsTr("The playlist's file in Music/Playlists is renamed too.") : qsTr("Playlists are kept in Music/Playlists, where other players can open them too.")

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
        property string playlist
        function ask(name) {
            playlist = name;
            open();
        }
        title: qsTr("Delete %1?").arg(playlist)
        text: qsTr("The playlist goes to the bin. The songs in it stay in your Music folder.")

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
                    const name = deleteDialog.playlist;
                    deleteDialog.close();
                    if (Playlists.remove(name)) {
                        window.say(qsTr("%1 moved to the bin").arg(name), qsTr("Undo"), () => {
                            if (!Playlists.restore(name)) {
                                window.say(Playlists.lastError);
                            }
                        });
                    } else {
                        window.say(Playlists.lastError);
                    }
                }
            }
        ]
    }
}
