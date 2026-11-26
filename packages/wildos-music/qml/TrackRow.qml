import QtQuick
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// A song in a track table (AppMusic.dc): 40px, radius 6, the number, the
// title, the artist, the heart and the length. The song that is playing
// has the neutral highlight, moving bars in place of its number and its
// title in the accent. Under the pointer the number becomes a play button;
// a double click plays the song too.
Item {
    id: row

    property var track: ({})
    property int number: 0
    property bool showAlbum: false
    property bool selected: false

    readonly property bool isCurrent: track.path !== undefined && track.path === Player.currentPath
    property real artistWidth: 200

    signal play()
    signal pressed(int button, int modifiers)
    signal menuRequested(real x, real y)

    implicitHeight: 40
    Accessible.role: Accessible.Row
    Accessible.name: qsTr("%1, %2, %3").arg(track.title).arg(track.artist).arg(MusicStyle.time(track.duration))
    Accessible.selected: selected

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusXs
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: mouse.containsMouse
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.isCurrent || row.selected ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function (event) {
            row.pressed(event.button, event.modifiers);
            if (event.button === Qt.RightButton) {
                row.menuRequested(event.x, event.y);
            }
        }
        onDoubleClicked: function (event) {
            if (event.button === Qt.LeftButton) {
                row.play();
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 14

        Item {
            Layout.preferredWidth: 22
            Layout.fillHeight: true

            UI.Label {
                anchors.centerIn: parent
                visible: !row.isCurrent && !mouse.containsMouse && !playHere.hovered
                text: row.number > 0 ? row.number : ""
                size: 13
                color: UI.Style.inkSubtle
            }
            Bars {
                anchors.centerIn: parent
                visible: row.isCurrent && !mouse.containsMouse && !playHere.hovered
                running: Player.playing
            }
            ToolButton {
                id: playHere
                anchors.centerIn: parent
                visible: mouse.containsMouse || hovered
                side: 26
                iconSize: 13
                iconName: row.isCurrent && Player.playing ? "pause" : "play"
                tip: row.isCurrent && Player.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: row.isCurrent ? Player.playPause() : row.play()
            }
        }

        UI.Label {
            Layout.fillWidth: true
            text: row.track.title ?? ""
            size: 13.5
            color: row.isCurrent ? MusicStyle.playingInk : UI.Style.ink
            font.weight: row.isCurrent ? UI.Style.weightSemiBold : UI.Style.weightRegular
        }
        UI.Label {
            Layout.preferredWidth: row.artistWidth
            text: row.track.artist ?? ""
            size: 13
            color: UI.Style.inkMuted
        }
        UI.Label {
            visible: row.showAlbum
            Layout.preferredWidth: row.artistWidth
            text: row.track.album ?? ""
            size: 13
            color: UI.Style.inkMuted
        }
        // The board's column is the 15px heart; the button around it is
        // larger so it is easy to hit.
        Item {
            Layout.preferredWidth: 15
            Layout.fillHeight: true

            HeartButton {
                anchors.centerIn: parent
                side: 28
                paths: row.track.path ? [row.track.path] : []
            }
        }
        UI.Label {
            Layout.preferredWidth: 50
            horizontalAlignment: Text.AlignRight
            text: row.track.duration > 0 ? MusicStyle.time(row.track.duration) : ""
            size: 13
            color: UI.Style.inkMuted
        }
    }
}
