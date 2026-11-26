import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// The left column (AppMusic.dc): Search and Home, the library by songs,
// albums, artists and folders with the song count, then Liked songs and
// the playlists, New playlist, and the Music folder card at the foot.
Rectangle {
    id: sidebar

    property string kind
    property string key

    signal navigate(string kind, string key)
    signal newPlaylist()
    signal playlistMenu(string name, Item row, real x, real y)

    color: MusicStyle.sideShade

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: UI.Style.borderSubtle
    }

    // Tab can reach a row that is scrolled out of sight.
    Connections {
        target: sidebar.Window.window
        function onActiveFocusItemChanged() {
            const row = sidebar.Window.window.activeFocusItem;
            if (!row || row.parent !== column) {
                return;
            }
            if (row.y < flick.contentY) {
                flick.contentY = row.y;
            } else if (row.y + row.height > flick.contentY + flick.height) {
                flick.contentY = row.y + row.height - flick.height;
            }
        }
    }

    Flickable {
        id: flick
        anchors.top: parent.top
        anchors.topMargin: 4
        anchors.bottom: card.top
        anchors.bottomMargin: 8
        width: parent.width - 1
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: UI.ScrollBar {}

        Column {
            id: column
            width: flick.width

            SideRow {
                width: parent.width
                iconName: "search"
                text: qsTr("Search")
                current: sidebar.kind === "search"
                onClicked: sidebar.navigate("search", "")
            }
            SideRow {
                width: parent.width
                iconName: "home"
                text: qsTr("Home")
                current: sidebar.kind === "home"
                onClicked: sidebar.navigate("home", "")
            }

            SideHeading {
                width: parent.width
                text: qsTr("Library")
            }
            SideRow {
                width: parent.width
                iconName: "song"
                text: qsTr("Songs")
                count: Library.songCount
                current: sidebar.kind === "songs"
                onClicked: sidebar.navigate("songs", "")
            }
            SideRow {
                width: parent.width
                iconName: "album"
                text: qsTr("Albums")
                current: sidebar.kind === "albums" || sidebar.kind === "album"
                onClicked: sidebar.navigate("albums", "")
            }
            SideRow {
                width: parent.width
                iconName: "artist"
                text: qsTr("Artists")
                current: sidebar.kind === "artists" || sidebar.kind === "artist"
                onClicked: sidebar.navigate("artists", "")
            }
            SideRow {
                width: parent.width
                iconName: "folder"
                text: qsTr("Folders")
                current: sidebar.kind === "folders"
                onClicked: sidebar.navigate("folders", "")
            }

            SideHeading {
                width: parent.width
                text: qsTr("Playlists")
            }
            SideRow {
                width: parent.width
                iconName: "heart"
                text: qsTr("Liked songs")
                count: Playlists.likedCount
                current: sidebar.kind === "liked"
                onClicked: sidebar.navigate("liked", "")
            }
            Repeater {
                model: Playlists.all
                delegate: SideRow {
                    id: playlistRow
                    required property var modelData
                    width: column.width
                    iconName: "playlist"
                    text: modelData.name
                    current: sidebar.kind === "playlist" && sidebar.key === modelData.name
                    onClicked: sidebar.navigate("playlist", modelData.name)
                    onMenuRequested: (x, y) => sidebar.playlistMenu(modelData.name, playlistRow, x, y)
                }
            }
            SideRow {
                width: parent.width
                iconName: "plus"
                text: qsTr("New playlist")
                onClicked: sidebar.newPlaylist()
            }
        }
    }

    // Opens the Music folder in Files: the songs are plain files there,
    // and nothing here comes from anywhere else.
    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 13
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        height: cardRow.implicitHeight + 22
        radius: 10
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: cardMouse.containsMouse
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: cardMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceRaised
        border.width: 1
        border.color: UI.Style.borderSubtle
        Accessible.role: Accessible.Button
        Accessible.name: cardTitle.text + ". " + cardText.text

        RowLayout {
            id: cardRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            MusicIcon {
                name: "folder-open"
                size: 18
                color: UI.Style.inkMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    id: cardTitle
                    Layout.fillWidth: true
                    text: qsTr("Music folder")
                    size: 12
                    font.weight: UI.Style.weightSemiBold
                }
                UI.Label {
                    id: cardText
                    Layout.fillWidth: true
                    text: qsTr("Your files, nothing streamed")
                    size: 11
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                }
            }
        }

        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Library.openMusicFolder()
        }

        UI.Tooltip {
            text: qsTr("Open the Music folder in Files")
            visible: cardMouse.containsMouse
        }
    }
}
