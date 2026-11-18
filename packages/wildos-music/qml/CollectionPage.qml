import QtQuick
import WildOS.Music
import WildOS.UI as UI

// An album, an artist, a playlist, Liked songs or every song: the top from
// the board's album page, then the songs.
TrackList {
    id: page

    property var app
    // "album", "artist", "playlist", "liked" or "songs".
    property string kind
    property string key
    property string sort: "title"

    readonly property bool isPlaylist: kind === "playlist" || kind === "liked"
    // The playlist's file name, or "" on a page that is not a playlist.
    readonly property string playlist: !isPlaylist ? "" : kind === "liked" ? "Liked songs" : key
    readonly property var info: {
        Library.revision;
        if (page.isPlaylist) {
            Playlists.revision;
        }
        return page.load();
    }
    readonly property var paths: tracks.map(t => t.path)

    function load() {
        if (kind === "album") {
            const album = Library.album(key);
            if (!album.key) {
                return { missing: true, tracks: [] };
            }
            const facts = [album.artist];
            if (album.year > 0) {
                facts.push(album.year);
            }
            return {
                label: qsTr("Album"),
                title: album.title,
                facts: facts.join(" · ") + " · " + qsTr("%1, %2").arg(MusicStyle.songs(album.count)).arg(MusicStyle.length(album.duration)),
                cover: album.cover,
                tracks: album.tracks
            };
        }
        if (kind === "artist") {
            const artist = Library.artist(key);
            if (!artist.name) {
                return { missing: true, tracks: [] };
            }
            const facts = [];
            if (artist.albums.length > 0) {
                facts.push(artist.albums.length === 1 ? qsTr("1 album") : qsTr("%1 albums").arg(artist.albums.length));
            }
            facts.push(qsTr("%1, %2").arg(MusicStyle.songs(artist.count)).arg(MusicStyle.length(artist.duration)));
            return {
                label: qsTr("Artist"),
                title: artist.name,
                facts: facts.join(" · "),
                cover: artist.cover,
                round: true,
                tracks: artist.tracks
            };
        }
        if (isPlaylist) {
            const name = kind === "liked" ? "Liked songs" : key;
            const list = Library.tracks(Playlists.songs(name));
            return {
                label: qsTr("Playlist"),
                title: kind === "liked" ? qsTr("Liked songs") : key,
                facts: qsTr("%1, %2").arg(MusicStyle.songs(list.length)).arg(MusicStyle.length(list.reduce((sum, t) => sum + t.duration, 0))),
                cover: (list.find(t => t.cover.length > 0) ?? { cover: "" }).cover,
                placeholder: kind === "liked" ? "heart" : "playlist",
                tracks: list
            };
        }
        const all = Library.songs(sort);
        return {
            label: qsTr("Library"),
            title: qsTr("Songs"),
            facts: qsTr("%1, %2").arg(MusicStyle.songs(all.length)).arg(MusicStyle.length(all.reduce((sum, t) => sum + t.duration, 0))),
            cover: "",
            placeholder: "song",
            tracks: all
        };
    }

    tracks: info.tracks
    numbering: kind === "album" ? "track" : "index"
    showAlbum: kind !== "album"

    onPlayAt: index => app.play(paths, index, false)
    onMenuAt: (index, row, x, y) => {
        if (selectedCount > 1) {
            app.tracksMenu(picked(), playlist, row, x, y);
        } else {
            app.trackMenu(tracks[index], playlist, row, x, y);
        }
    }
    // Delete takes songs out of a playlist; it never deletes a song's file.
    onDeleteRequested: {
        if (isPlaylist) {
            app.removeFromPlaylist(playlist, picked().map(t => t.path));
        }
    }

    heading: kind === "songs" ? songsTop : collectionTop

    Component {
        id: songsTop

        PageHeading {
            title: qsTr("Songs")
            subtitle: page.info.facts ?? ""

            PageButton {
                primary: true
                iconName: "play"
                text: qsTr("Play")
                enabled: page.tracks.length > 0
                onClicked: page.app.play(page.paths, 0, false)
            }
            PageButton {
                iconName: "shuffle"
                text: qsTr("Shuffle")
                enabled: page.tracks.length > 0
                onClicked: page.app.play(page.paths, -1, true)
            }
            PageButton {
                id: sortButton
                iconName: "sort"
                text: page.sort === "artist" ? qsTr("Artist") : page.sort === "album" ? qsTr("Album") : page.sort === "added" ? qsTr("Recently added") : qsTr("Title")
                onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4)

                UI.Menu {
                    id: sortMenu
                    entries: [
                        { text: qsTr("Title"), radio: true, checked: page.sort === "title", run: () => page.sort = "title" },
                        { text: qsTr("Artist"), radio: true, checked: page.sort === "artist", run: () => page.sort = "artist" },
                        { text: qsTr("Album"), radio: true, checked: page.sort === "album", run: () => page.sort = "album" },
                        { text: qsTr("Recently added"), radio: true, checked: page.sort === "added", run: () => page.sort = "added" }
                    ]
                }
            }
        }
    }

    Component {
        id: collectionTop

        CollectionTop {
            cover: page.info.cover ?? ""
            round: page.info.round ?? false
            placeholder: page.info.placeholder ?? (page.kind === "artist" ? "artist" : "album")
            label: page.info.label ?? ""
            title: page.info.title ?? ""
            facts: page.info.facts ?? ""
            paths: page.paths
            onPlayClicked: page.app.play(page.paths, 0, false)
            onShuffleClicked: page.app.play(page.paths, -1, true)
            onMoreClicked: button => page.app.collectionMenu(page.kind, page.key, page.paths, button)
        }
    }

    empty: Component {
        Column {
            width: parent ? parent.width : 0
            topPadding: 64
            spacing: 6

            MusicIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: page.kind === "liked" ? "heart" : "playlist"
                size: 40
                strokeWidth: 1.4
                color: UI.Style.inkSubtle
            }
            Item {
                width: 1
                height: 8
            }
            UI.Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: page.kind === "liked" ? qsTr("No liked songs yet") : page.isPlaylist ? qsTr("This playlist is empty") : qsTr("Nothing here")
                size: 15
                font.weight: UI.Style.weightSemiBold
            }
            UI.Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: page.kind === "liked" ? qsTr("Select the heart beside a song to keep it here.")
                    : page.isPlaylist ? qsTr("Right-click a song and choose Add to playlist.")
                    : qsTr("The songs may have been moved or deleted.")
                size: 13
                color: UI.Style.inkMuted
            }
        }
    }
}
