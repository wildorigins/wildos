import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// Home: what was played lately and the albums that arrived in the Music
// folder most recently, as covers. With an empty Music folder it says how
// to fill it.
Flickable {
    id: page

    property var app

    readonly property bool empty: Library.ready && Library.songCount === 0
    readonly property var recent: {
        Library.revision;
        const out = [];
        for (const entry of Player.recent) {
            if (entry.kind === "album") {
                const album = Library.album(entry.key);
                if (album.key) {
                    out.push({ kind: "album", key: album.key, title: album.title, subtitle: album.artist, cover: album.cover, paths: album.tracks.map(t => t.path) });
                }
            } else {
                const song = Library.track(entry.path);
                if (song.path) {
                    out.push({ kind: "song", key: song.path, title: song.title, subtitle: song.artist, cover: song.cover, paths: [song.path] });
                }
            }
            if (out.length >= 12) {
                break;
            }
        }
        return out;
    }
    readonly property var added: {
        Library.revision;
        return Library.recentlyAdded(12);
    }

    clip: true
    contentHeight: column.implicitHeight + 24
    boundsBehavior: Flickable.StopAtBounds
    T.ScrollBar.vertical: UI.ScrollBar {}

    // Tab can reach a cover that is scrolled out of sight.
    Connections {
        target: page.Window.window
        function onActiveFocusItemChanged() {
            const tile = page.Window.window.activeFocusItem;
            let inside = false;
            for (let up = tile; up && !inside; up = up.parent) {
                inside = up === column;
            }
            if (!inside) {
                return;
            }
            const top = tile.mapToItem(column, 0, 0).y - 12;
            const bottom = top + tile.height + 24;
            if (top < page.contentY) {
                page.contentY = Math.max(0, top);
            } else if (bottom > page.contentY + page.height) {
                page.contentY = bottom - page.height;
            }
        }
    }

    Column {
        id: column
        width: page.width
        spacing: 8

        PageHeading {
            width: parent.width
            title: qsTr("Home")
            subtitle: page.empty || !Library.ready ? "" : qsTr("%1 in your Music folder").arg(MusicStyle.songs(Library.songCount))
        }

        Section {
            app: page.app
            width: page.width
            title: qsTr("Recently played")
            items: page.recent
        }
        Section {
            app: page.app
            width: page.width
            title: qsTr("Recently added")
            items: page.added.map(a => ({ kind: "album", key: a.key, title: a.title, subtitle: a.artist, cover: a.cover }))
        }
    }

    component Section: Column {
        id: section

        // Inline components cannot see the page's ids, so it is handed in.
        property var app
        property string title
        property var items: []

        visible: items.length > 0
        spacing: 14

        UI.Label {
            x: 28
            topPadding: 12
            text: section.title
            size: 18
            font.weight: UI.Style.weightSemiBold
            Accessible.role: Accessible.Heading
        }
        Flow {
            x: 28
            width: parent.width - 56
            spacing: 24

            Repeater {
                model: section.items
                delegate: AlbumTile {
                    id: tile
                    required property var modelData
                    coverSize: 160
                    cover: modelData.cover ?? ""
                    title: modelData.title
                    subtitle: modelData.subtitle ?? ""
                    placeholder: modelData.kind === "song" ? "song" : "album"
                    onClicked: {
                        if (modelData.kind === "album") {
                            section.app.navigate("album", modelData.key);
                        } else {
                            section.app.play([modelData.key], 0, false);
                        }
                    }
                    onPlayClicked: section.app.play(modelData.paths ?? Library.album(modelData.key).tracks.map(t => t.path), 0, false)
                    onMenuRequested: (x, y) => section.app.collectionMenu(modelData.kind, modelData.key,
                        modelData.paths ?? Library.album(modelData.key).tracks.map(t => t.path), tile, x, y)
                }
            }
        }
    }

    // In the page itself rather than its column, so it stays centred in
    // what you can see.
    ColumnLayout {
        parent: page
        visible: page.empty
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 56)
        spacing: 8

        MusicIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
            name: "song"
            size: 40
            strokeWidth: 1.4
            color: UI.Style.inkSubtle
        }
        UI.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Your Music folder is empty")
            size: 18
            font.weight: UI.Style.weightSemiBold
        }
        UI.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Copy songs into the Music folder in your home folder and they show up here, with their albums, artists and covers. MP3, FLAC, Ogg, Opus, AAC and WAV all play.")
            size: 14
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
        PageButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            primary: true
            iconName: "folder-open"
            text: qsTr("Open Music folder")
            onClicked: Library.openMusicFolder()
        }
    }
}
