import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// The Music folder as a tree: folders open in place to show their own
// folders and songs. Only folders with songs somewhere in them are shown.
ListView {
    id: page

    property var app
    property var open: ({})

    readonly property var rows: {
        Library.revision;
        const out = [];
        const walk = (path, depth) => {
            for (const entry of Library.folder(path)) {
                out.push({ entry: entry, depth: depth, parent: path });
                if (entry.kind === "folder" && page.open[entry.path]) {
                    walk(entry.path, depth + 1);
                }
            }
        };
        walk("", 0);
        return out;
    }

    function toggle(path) {
        const next = Object.assign({}, open);
        if (next[path]) {
            delete next[path];
        } else {
            next[path] = true;
        }
        open = next;
    }
    function songsIn(folder) {
        return Library.folder(folder).filter(e => e.kind === "song").map(e => e.path);
    }
    function songsUnder(folder) {
        let paths = [];
        for (const e of Library.folder(folder)) {
            paths = paths.concat(e.kind === "song" ? [e.path] : songsUnder(e.path));
        }
        return paths;
    }

    clip: true
    focus: true
    model: rows
    boundsBehavior: Flickable.StopAtBounds
    T.ScrollBar.vertical: UI.ScrollBar {}
    bottomMargin: 16

    header: PageHeading {
        width: page.width
        title: qsTr("Folders")
        subtitle: Library.musicFolder.replace(/^\/home\/[^\/]+/, "~")
    }

    delegate: Item {
        id: row
        required property var modelData
        readonly property var entry: modelData.entry
        readonly property bool isFolder: entry.kind === "folder"
        readonly property bool isCurrent: !isFolder && entry.path === Player.currentPath

        width: page.width
        height: 40
        Accessible.role: Accessible.TreeItem
        Accessible.name: isFolder ? qsTr("%1, %2").arg(entry.name).arg(MusicStyle.songs(entry.count)) : entry.title

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            radius: UI.Style.radiusXs
            // Fades in on hover and clears at once, so moving along a list
            // never lights two items. It comes before the colour so it sees
            // the change in hover first.
            Behavior on color {
                enabled: mouse.containsMouse
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
            color: row.isCurrent ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function (event) {
                if (event.button === Qt.RightButton) {
                    if (row.isFolder) {
                        page.app.collectionMenu("folder", row.entry.path, page.songsUnder(row.entry.path), row, event.x, event.y);
                    } else {
                        page.app.trackMenu(row.entry, "", row, event.x, event.y);
                    }
                } else if (row.isFolder) {
                    page.toggle(row.entry.path);
                }
            }
            onDoubleClicked: function (event) {
                if (!row.isFolder && event.button === Qt.LeftButton) {
                    const songs = page.songsIn(row.modelData.parent);
                    page.app.play(songs, songs.indexOf(row.entry.path), false);
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 28 + row.modelData.depth * 22
            anchors.rightMargin: 28
            spacing: 10

            Item {
                Layout.preferredWidth: 14
                Layout.fillHeight: true

                MusicIcon {
                    visible: row.isFolder
                    anchors.centerIn: parent
                    name: page.open[row.entry.path] ? "chevron-down" : "chevron-right"
                    size: 14
                    color: UI.Style.inkSubtle
                }
            }
            Item {
                Layout.preferredWidth: 16
                Layout.fillHeight: true

                MusicIcon {
                    visible: !row.isCurrent
                    anchors.centerIn: parent
                    name: row.isFolder ? "folder" : "song"
                    size: 16
                    color: UI.Style.inkMuted
                }
                Bars {
                    visible: row.isCurrent
                    anchors.centerIn: parent
                    running: Player.playing
                }
            }
            UI.Label {
                Layout.fillWidth: true
                text: row.isFolder ? row.entry.name : row.entry.title
                size: 13.5
                color: row.isCurrent ? MusicStyle.playingInk : UI.Style.ink
                font.weight: row.isCurrent ? UI.Style.weightSemiBold : UI.Style.weightRegular
            }
            UI.Label {
                Layout.preferredWidth: Math.min(200, page.width * 0.2)
                text: row.isFolder ? MusicStyle.songs(row.entry.count) : row.entry.artist
                size: 13
                color: row.isFolder ? UI.Style.inkSubtle : UI.Style.inkMuted
            }
            Item {
                Layout.preferredWidth: 15
                Layout.fillHeight: true

                HeartButton {
                    visible: !row.isFolder
                    anchors.centerIn: parent
                    side: 28
                    paths: row.isFolder ? [] : [row.entry.path]
                }
            }
            UI.Label {
                Layout.preferredWidth: 50
                horizontalAlignment: Text.AlignRight
                text: !row.isFolder && row.entry.duration > 0 ? MusicStyle.time(row.entry.duration) : ""
                size: 13
                color: UI.Style.inkMuted
            }
        }
    }
}
