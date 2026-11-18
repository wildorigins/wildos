import QtQuick
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// Search through titles, artists and albums as you type: matching artists
// and albums as covers, then the songs. The field stays above the results;
// inside the list it would lose the keyboard each time the list refilled.
Item {
    id: page

    property var app
    property string text

    readonly property var results: {
        Library.revision;
        return text.trim().length > 0 ? Library.search(text) : ({ songs: [], albums: [], artists: [] });
    }

    function focusField() {
        field.forceActiveFocus();
        field.selectAll();
    }

    Component.onCompleted: focusField()

    PageHeading {
        id: heading
        width: parent.width
        title: qsTr("Search")
    }

    UI.SearchField {
        id: field
        anchors.top: heading.bottom
        x: 28
        width: Math.min(520, parent.width - 56)
        placeholderText: qsTr("Songs, albums and artists")
        text: page.text
        onTextChanged: page.text = text
        Keys.onDownPressed: {
            if (list.count > 0) {
                list.select(0, Qt.NoModifier);
                list.forceActiveFocus();
            }
        }
    }

    TrackList {
        id: list
        anchors.top: field.bottom
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        focus: false
        tracks: page.results.songs ?? []
        showAlbum: true
        readonly property var paths: tracks.map(t => t.path)

        onPlayAt: index => page.app.play(paths, index, false)
        onMenuAt: (index, row, x, y) => {
            if (selectedCount > 1) {
                page.app.tracksMenu(picked(), "", row, x, y);
            } else {
                page.app.trackMenu(tracks[index], "", row, x, y);
            }
        }

        heading: Component {
            Column {
                spacing: 16
                bottomPadding: 8

                UI.Label {
                    x: 28
                    visible: page.text.trim().length > 0 && list.tracks.length === 0 && (page.results.albums ?? []).length === 0 && (page.results.artists ?? []).length === 0
                    text: qsTr("Nothing in your Music folder matches “%1”.").arg(page.text.trim())
                    size: 14
                    color: UI.Style.inkMuted
                }
                Results {
                    width: parent.width
                    title: qsTr("Artists")
                    round: true
                    items: page.results.artists ?? []
                    onOpened: item => page.app.navigate("artist", item.name)
                    onPlayed: item => page.app.play(Library.artist(item.name).tracks.map(t => t.path), 0, false)
                }
                Results {
                    width: parent.width
                    title: qsTr("Albums")
                    items: page.results.albums ?? []
                    onOpened: item => page.app.navigate("album", item.key)
                    onPlayed: item => page.app.play(Library.album(item.key).tracks.map(t => t.path), 0, false)
                }
                UI.Label {
                    x: 28
                    visible: list.tracks.length > 0
                    text: qsTr("Songs")
                    size: 18
                    font.weight: UI.Style.weightSemiBold
                    Accessible.role: Accessible.Heading
                }
            }
        }
    }

    component Results: Column {
        id: results

        property string title
        property var items: []
        property bool round: false

        signal opened(var item)
        signal played(var item)

        visible: items.length > 0
        spacing: 14

        UI.Label {
            x: 28
            topPadding: 8
            text: results.title
            size: 18
            font.weight: UI.Style.weightSemiBold
            Accessible.role: Accessible.Heading
        }
        Flow {
            x: 28
            width: parent.width - 56
            spacing: 24

            Repeater {
                model: results.items
                delegate: AlbumTile {
                    required property var modelData
                    coverSize: 140
                    round: results.round
                    placeholder: results.round ? "artist" : "album"
                    cover: modelData.cover ?? ""
                    title: results.round ? modelData.name : modelData.title
                    subtitle: results.round ? MusicStyle.songs(modelData.count) : modelData.artist
                    onClicked: results.opened(modelData)
                    onPlayClicked: results.played(modelData)
                }
            }
        }
    }
}
