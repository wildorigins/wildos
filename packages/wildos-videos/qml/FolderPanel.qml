import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Videos
import WildOS.UI as UI

// The side panel (AppVideos.dc): "In this folder" with the other videos
// beside the one playing, buttons to show the folder in Files and to hide
// the panel, and a note at the foot that places are kept on this PC.
Item {
    id: panel

    signal hideRequested()
    signal menuRequested(string path, Item item, real x, real y)

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: UI.Style.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.bottomMargin: 6
            spacing: 0

            UI.Label {
                Layout.fillWidth: true
                text: Player.queued ? qsTr("Queue") : qsTr("In this folder")
                size: 14
                font.weight: UI.Style.weightSemiBold
            }
            PanelButton {
                iconName: "folder-open"
                tip: qsTr("Show in Files")
                onClicked: Library.showInFiles(Player.path)
            }
            PanelButton {
                iconName: "panel-close"
                tip: qsTr("Hide the list")
                onClicked: panel.hideRequested()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftMargin: 8
            rightMargin: 8
            spacing: 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Player.folderVideos
            currentIndex: Player.folderVideos.indexOf(Player.path)
            highlightFollowsCurrentItem: false
            T.ScrollBar.vertical: UI.ScrollBar {}

            delegate: VideoRow {
                width: list.width - list.leftMargin - list.rightMargin
                onMenuRequested: (path, item, x, y) => panel.menuRequested(path, item, x, y)
            }

            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            Layout.topMargin: 8
            implicitHeight: note.implicitHeight + 20
            radius: 10
            color: UI.Style.surfaceRaised
            border.width: 1
            border.color: UI.Style.borderSubtle

            RowLayout {
                id: note
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                VideosIcon {
                    name: "history"
                    size: 16
                    color: UI.Style.inkMuted
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Picks up where you left off, on this PC only")
                    size: 12
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
