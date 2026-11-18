import QtQuick
import QtQuick.Layouts
import WildOS.Videos
import WildOS.UI as UI

// A video in the side panel (AppVideos.dc): its still, the name, and under
// it "Playing" in the accent, "Watched", or the folder it is in.
Rectangle {
    id: row

    required property string modelData
    readonly property string path: modelData
    readonly property bool current: path === Player.path
    readonly property bool watched: (Positions.revision, Positions.watched(path))

    signal menuRequested(string path, Item item, real x, real y)

    implicitHeight: 84
    radius: UI.Style.radiusSm
    color: current ? VideosStyle.rowSelected : mouse.containsMouse ? VideosStyle.rowHover : "transparent"
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: title.text
    Accessible.selected: current
    Accessible.onPressAction: row.play()
    Keys.onReturnPressed: row.play()
    Keys.onEnterPressed: row.play()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            row.menuRequested(row.path, row, 24, 24);
            event.accepted = true;
        }
    }

    function play() {
        if (!current) {
            Player.open(path);
        }
    }

    // Inside the row: the list clips, and the rows nearly touch.
    UI.FocusRing {
        anchors.margins: 0
        targetRadius: row.radius - 4
        visible: row.activeFocus
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        Thumbnail {
            Layout.preferredWidth: 120
            Layout.preferredHeight: 68
            path: row.path
            liveProgress: row.current && Player.duration > 0 ? Player.position / Player.duration : -1
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            UI.Label {
                id: title
                Layout.fillWidth: true
                text: Library.nameOf(row.path)
                size: 13
                font.weight: row.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
            }
            UI.Label {
                Layout.fillWidth: true
                size: 12
                text: row.current ? qsTr("Playing") : row.watched ? qsTr("Watched") : Library.nameOf(Library.parentOf(row.path))
                color: row.current ? UI.Style.accentText : UI.Style.inkMuted
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: event => {
            if (event.button === Qt.RightButton) {
                row.menuRequested(row.path, row, event.x, event.y);
            } else {
                row.play();
            }
        }
    }
}
