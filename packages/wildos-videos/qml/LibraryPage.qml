import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Videos
import WildOS.UI as UI

// What Videos shows when it is opened without a video: what you watched
// last, the folders in Videos and the videos loose in it, as tiles in the
// side panel's style. A folder opens in place, with Back to return.
Item {
    id: page

    property string folder: Library.videosFolder
    readonly property bool atTop: folder === Library.videosFolder
    property var recent: []
    property var folders: []
    property var videos: []

    signal openRequested()
    signal menuRequested(string path, Item item, real x, real y)

    function reload() {
        recent = atTop ? Positions.recent(8) : [];
        folders = Library.foldersIn(folder);
        videos = Library.videosIn(folder);
        Library.watch(folder);
    }
    onFolderChanged: {
        reload();
        flick.contentY = 0;
    }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()

    Connections {
        target: Library
        function onFolderChanged(changed) {
            if (changed === page.folder) {
                page.reload();
            }
        }
    }

    // Tab can reach a tile that is scrolled out of sight.
    Connections {
        target: page.Window.window
        function onActiveFocusItemChanged() {
            const tile = page.Window.window.activeFocusItem;
            let inside = false;
            for (let up = tile; up && !inside; up = up.parent) {
                inside = up === body;
            }
            if (!inside) {
                return;
            }
            const top = tile.mapToItem(flick.contentItem, 0, 0).y - 12;
            const bottom = top + tile.height + 24;
            if (top < flick.contentY) {
                flick.contentY = Math.max(0, top);
            } else if (bottom > flick.contentY + flick.height) {
                flick.contentY = bottom - flick.height;
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: UI.ScrollBar {}

        ColumnLayout {
            id: body
            x: 32
            y: 24
            width: flick.width - 64
            spacing: 28

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                PanelButton {
                    visible: !page.atTop
                    iconName: "back"
                    tip: qsTr("Back")
                    onClicked: page.folder = Library.parentOf(page.folder)
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    UI.Label {
                        Layout.fillWidth: true
                        text: page.atTop ? qsTr("Videos") : Library.nameOf(page.folder) || page.folder
                        size: 26
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        visible: !page.atTop
                        text: Library.placeOf(Library.parentOf(page.folder))
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                }
                PanelButton {
                    iconName: "folder-open"
                    tip: qsTr("Show in Files")
                    onClicked: Library.showInFiles(page.folder)
                }
                UI.Button {
                    kind: "primary"
                    text: qsTr("Open a video")
                    onClicked: page.openRequested()
                }
            }

            Section {
                visible: page.recent.length > 0
                title: qsTr("Recent")

                Repeater {
                    model: page.recent
                    VideoTile {
                        required property string modelData
                        path: modelData
                        detail: Library.placeOf(Library.parentOf(modelData))
                        onMenuRequested: (path, item, x, y) => page.menuRequested(path, item, x, y)
                    }
                }
            }

            Section {
                visible: page.folders.length > 0
                title: qsTr("Folders")

                Repeater {
                    model: page.folders
                    FolderTile {
                        required property var modelData
                        folder: modelData
                        onOpened: page.folder = modelData.path
                    }
                }
            }

            Section {
                visible: page.videos.length > 0
                title: page.atTop ? qsTr("In Videos") : VideosStyle.count(page.videos.length)

                Repeater {
                    model: page.videos
                    VideoTile {
                        required property string modelData
                        path: modelData
                        onMenuRequested: (path, item, x, y) => page.menuRequested(path, item, x, y)
                    }
                }
            }

        }
    }

    // Centred in the page rather than placed in the column, so an empty
    // folder reads as a message, not as a list that failed to load.
    ColumnLayout {
        visible: page.recent.length === 0 && page.folders.length === 0 && page.videos.length === 0
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 64)
        spacing: 8

        VideosIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
            name: "film"
            size: 40
            strokeWidth: 1.4
            color: UI.Style.inkSubtle
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            text: page.atTop ? qsTr("No videos in your Videos folder yet") : qsTr("No videos in this folder")
            size: 16
            font.weight: UI.Style.weightSemiBold
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Videos you save in Videos show here. You can also open a video from anywhere on this PC, or double-click one in Files.")
            size: 13
            color: UI.Style.inkMuted
        }
    }

    component Section: ColumnLayout {
        id: section
        property string title
        default property alias tiles: flow.data

        Layout.fillWidth: true
        spacing: 12

        UI.Label {
            text: section.title
            size: 15
            font.weight: UI.Style.weightSemiBold
        }
        Flow {
            id: flow
            Layout.fillWidth: true
            spacing: 16
        }
    }
}
