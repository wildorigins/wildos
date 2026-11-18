import QtQuick
import QtQuick.Layouts
import WildOS.Videos
import WildOS.UI as UI

// A folder in the library: the still of its first video with a folder
// badge, the name and how many videos it holds.
Rectangle {
    id: tile

    // { path, name, count, cover } from Library.foldersIn().
    property var folder: ({})

    signal opened()

    implicitWidth: 256
    implicitHeight: column.implicitHeight + 16
    radius: UI.Style.radiusSm
    color: mouse.containsMouse ? VideosStyle.rowHover : "transparent"
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: tile.folder.name ?? ""
    Accessible.onPressAction: tile.opened()
    Keys.onReturnPressed: tile.opened()
    Keys.onEnterPressed: tile.opened()
    Keys.onSpacePressed: tile.opened()

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
            path: tile.folder.cover ?? ""
            showLength: false
            liveProgress: 0

            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 6
                width: badge.implicitWidth + 12
                height: 22
                radius: 4
                color: VideosStyle.durationChip

                RowLayout {
                    id: badge
                    anchors.centerIn: parent
                    spacing: 5

                    VideosIcon {
                        name: "folder"
                        size: 12
                        color: VideosStyle.overPicture
                    }
                    UI.Label {
                        size: 11
                        color: VideosStyle.overPicture
                        text: Number(tile.folder.count ?? 0).toLocaleString(Qt.locale(), "f", 0)
                    }
                }
            }
        }
        UI.Label {
            Layout.fillWidth: true
            text: tile.folder.name ?? ""
            size: 13
        }
        UI.Label {
            Layout.fillWidth: true
            text: VideosStyle.count(tile.folder.count ?? 0)
            size: 12
            color: UI.Style.inkMuted
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.opened()
    }
}
