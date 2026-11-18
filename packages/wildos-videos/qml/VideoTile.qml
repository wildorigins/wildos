import QtQuick
import QtQuick.Layouts
import WildOS.Videos
import WildOS.UI as UI

// A video in the library: its still with the length and how far it was
// watched, the name, and under it "Watched", or `detail` (where it is).
Rectangle {
    id: tile

    property string path
    property string detail
    readonly property bool watched: (Positions.revision, Positions.watched(path))

    signal menuRequested(string path, Item item, real x, real y)

    implicitWidth: 256
    implicitHeight: column.implicitHeight + 16
    radius: UI.Style.radiusSm
    color: mouse.containsMouse ? VideosStyle.rowHover : "transparent"
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: title.text
    Accessible.onPressAction: Player.open(tile.path)
    Keys.onReturnPressed: Player.open(tile.path)
    Keys.onEnterPressed: Player.open(tile.path)
    Keys.onSpacePressed: Player.open(tile.path)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            tile.menuRequested(tile.path, tile, 24, 24);
            event.accepted = true;
        }
    }

    UI.FocusRing {
        visible: tile.activeFocus
        targetRadius: tile.radius
    }

    ColumnLayout {
        id: column
        x: 8
        y: 8
        width: parent.width - 16
        spacing: 3

        Thumbnail {
            Layout.preferredWidth: 240
            Layout.preferredHeight: 135
            Layout.bottomMargin: 6
            radius: 8
            path: tile.path
        }
        UI.Label {
            id: title
            Layout.fillWidth: true
            text: Library.nameOf(tile.path)
            size: 13
        }
        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: tile.watched ? qsTr("Watched") : tile.detail
            size: 12
            color: UI.Style.inkMuted
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
                tile.menuRequested(tile.path, tile, event.x, event.y);
            } else {
                Player.open(tile.path);
            }
        }
    }
}
