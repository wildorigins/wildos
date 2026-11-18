import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// The column the queue and lyrics buttons open at the right of the page:
// the songs still to play, which can be jumped to or taken out, or the
// playing song's own lyrics.
Rectangle {
    id: panel

    property string mode
    property var app

    signal closeRequested()

    readonly property var songs: {
        Library.revision;
        return panel.mode === "queue" ? Library.tracks(Player.queue) : [];
    }
    // The song the keyboard is on in the queue, and whether the keyboard
    // put it there, which is when its outline shows.
    property int cursor: -1
    property bool cursorByKeys: false
    // A song being dragged by its grip: where it started, and the gap it
    // would land in, counted from 0 above the first song.
    property int dragFrom: -1
    property int dragGap: -1
    readonly property bool dragMoves: dragFrom >= 0 && dragGap >= 0 && dragGap !== dragFrom && dragGap !== dragFrom + 1

    // The list is filled again each time the queue changes, which would
    // send it back to the top.
    function keepingPlace(change) {
        const y = queue.contentY;
        change();
        queue.contentY = y;
        Qt.callLater(() => queue.returnToBounds());
    }
    function moveSong(from, to) {
        if (from < 0 || to < 0 || to >= queue.count || from === to) {
            return;
        }
        keepingPlace(() => Player.moveInQueue(from, to));
        cursor = to;
    }
    function removeSong(index) {
        keepingPlace(() => Player.removeFromQueue(index));
    }
    function gapAt(y) {
        const inView = Math.max(queue.contentY, Math.min(queue.contentY + queue.height - 1, y));
        const at = queue.indexAt(queue.width / 2, inView);
        const row = at >= 0 ? queue.itemAtIndex(at) : null;
        if (!row) {
            return queue.count;
        }
        return inView > row.y + row.height / 2 ? at + 1 : at;
    }

    Connections {
        target: Player
        function onQueueChanged() {
            panel.dragFrom = -1;
            panel.dragGap = -1;
            if (panel.cursor >= Player.queue.length) {
                panel.cursor = Player.queue.length - 1;
            }
        }
    }

    color: MusicStyle.sideShade

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: UI.Style.borderSubtle
    }

    RowLayout {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.rightMargin: 12
        anchors.topMargin: 18
        spacing: 8

        UI.Label {
            Layout.fillWidth: true
            text: panel.mode === "lyrics" ? qsTr("Lyrics") : qsTr("Queue")
            size: 15
            font.weight: UI.Style.weightSemiBold
            Accessible.role: Accessible.Heading
        }
        UI.Button {
            visible: panel.mode === "queue" && Player.index + 1 < Player.queue.length
            kind: "text"
            size: "small"
            text: qsTr("Clear")
            onClicked: Player.clearUpNext()
        }
        ToolButton {
            iconName: "x"
            side: 30
            tip: qsTr("Close")
            onClicked: panel.closeRequested()
        }
    }

    Flickable {
        id: lyrics
        visible: panel.mode === "lyrics"
        anchors.top: heading.bottom
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentHeight: lyricsText.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: UI.ScrollBar {}

        UI.Label {
            id: lyricsText
            x: 20
            width: lyrics.width - 40
            text: {
                const path = Player.currentPath;
                return panel.mode === "lyrics" && path.length > 0 ? Library.lyrics(path) : "";
            }
            size: 15
            lineHeight: 1.45
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            color: UI.Style.ink
        }
        UI.Label {
            x: 20
            visible: lyricsText.text.length === 0
            width: lyrics.width - 40
            text: qsTr("This song has no lyrics in its file.")
            size: 13
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
    }

    ListView {
        id: queue
        visible: panel.mode === "queue"
        anchors.top: heading.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: UI.ScrollBar {}
        bottomMargin: 12
        model: panel.songs
        activeFocusOnTab: true
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Queue")

        function step(by) {
            if (count === 0) {
                return;
            }
            panel.cursor = Math.max(0, Math.min(count - 1, panel.cursor + by));
            panel.cursorByKeys = true;
            positionViewAtIndex(panel.cursor, ListView.Contain);
        }
        onActiveFocusChanged: {
            if (activeFocus) {
                panel.cursorByKeys = true;
                if (panel.cursor < 0 && count > 0) {
                    panel.cursor = Math.max(0, Player.index);
                }
            }
        }
        Keys.onUpPressed: event => {
            if (event.modifiers & Qt.AltModifier) {
                panel.moveSong(panel.cursor, panel.cursor - 1);
            } else {
                step(-1);
            }
        }
        Keys.onDownPressed: event => {
            if (event.modifiers & Qt.AltModifier) {
                panel.moveSong(panel.cursor, panel.cursor + 1);
            } else {
                step(1);
            }
        }
        Keys.onReturnPressed: if (panel.cursor >= 0) Player.jump(panel.cursor)
        Keys.onEnterPressed: if (panel.cursor >= 0) Player.jump(panel.cursor)
        Keys.onDeletePressed: if (panel.cursor >= 0 && panel.cursor !== Player.index) panel.removeSong(panel.cursor)
        Keys.onPressed: event => {
            if ((event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) && panel.cursor >= 0) {
                const row = itemAtIndex(panel.cursor);
                if (row) {
                    panel.app.trackMenu(panel.songs[panel.cursor], "", row, 40, row.height - 8);
                }
                event.accepted = true;
            }
        }

        delegate: Item {
            id: entry
            required property var modelData
            required property int index
            readonly property bool isCurrent: index === Player.index
            readonly property bool played: index < Player.index

            width: queue.width
            height: (index === Player.index || index === Player.index + 1 ? 30 : 0) + 52

            Rectangle {
                readonly property bool below: panel.dragGap === queue.count && entry.index === queue.count - 1
                visible: panel.dragMoves && (panel.dragGap === entry.index || below)
                x: 8
                y: below ? parent.height - height : 0
                z: 1
                width: parent.width - 16
                height: 2
                radius: 1
                color: UI.Style.accent
            }

            UI.Label {
                visible: entry.index === Player.index || entry.index === Player.index + 1
                x: 20
                height: 30
                text: entry.isCurrent ? qsTr("Now playing") : qsTr("Up next")
                size: 12
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkSubtle
            }

            Item {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                height: 52
                opacity: panel.dragFrom === entry.index ? 0.4 : entry.played ? 0.55 : 1

                Rectangle {
                    anchors.fill: parent
                    radius: UI.Style.radiusXs
                    color: entry.isCurrent || (queue.activeFocus && panel.cursor === entry.index) ? UI.Style.surfaceSelected
                         : rowMouse.containsMouse ? UI.Style.surfaceHover : "transparent"

                    // Inside the row: the list clips, and the rows touch.
                    UI.FocusRing {
                        anchors.margins: 0
                        targetRadius: UI.Style.radiusXs - 4
                        visible: queue.activeFocus && panel.cursorByKeys && panel.cursor === entry.index
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: {
                        queue.forceActiveFocus();
                        panel.cursor = entry.index;
                        panel.cursorByKeys = false;
                    }
                    onClicked: function (event) {
                        if (event.button === Qt.RightButton) {
                            panel.app.trackMenu(entry.modelData, "", rowMouse, event.x, event.y);
                        }
                    }
                    onDoubleClicked: Player.jump(entry.index)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 6
                    spacing: 10

                    Cover {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        radius: 4
                        source: entry.modelData.cover

                        Bars {
                            anchors.centerIn: parent
                            visible: entry.isCurrent && parent.hasArt
                            color: "#ffffff"
                            running: Player.playing
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        UI.Label {
                            Layout.fillWidth: true
                            text: entry.modelData.title
                            size: 13
                            color: entry.isCurrent ? MusicStyle.playingInk : UI.Style.ink
                            font.weight: entry.isCurrent ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: entry.modelData.artist
                            size: 12
                            color: UI.Style.inkMuted
                        }
                    }
                    Item {
                        visible: queue.count > 1 && (rowMouse.containsMouse || gripMouse.containsMouse || gripMouse.pressed || takeOut.hovered)
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 28
                        Accessible.role: Accessible.Grouping
                        Accessible.name: gripTip.text

                        MusicIcon {
                            anchors.centerIn: parent
                            name: "grip"
                            size: 16
                            color: gripMouse.containsMouse || gripMouse.pressed ? UI.Style.iconInk : UI.Style.inkMuted
                        }
                        MouseArea {
                            id: gripMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            onPressed: {
                                panel.cursor = entry.index;
                                panel.cursorByKeys = false;
                                panel.dragFrom = entry.index;
                                panel.dragGap = entry.index;
                            }
                            onPositionChanged: event => {
                                if (pressed) {
                                    panel.dragGap = panel.gapAt(mapToItem(queue.contentItem, event.x, event.y).y);
                                }
                            }
                            onReleased: {
                                const from = panel.dragFrom;
                                const gap = panel.dragGap;
                                const moves = panel.dragMoves;
                                panel.dragFrom = -1;
                                panel.dragGap = -1;
                                if (moves) {
                                    panel.moveSong(from, gap > from ? gap - 1 : gap);
                                }
                            }
                            onCanceled: {
                                panel.dragFrom = -1;
                                panel.dragGap = -1;
                            }
                        }
                        UI.Tooltip {
                            id: gripTip
                            text: qsTr("Drag to move, or press Alt+Up or Alt+Down")
                            visible: gripMouse.containsMouse && !gripMouse.pressed
                        }
                    }
                    ToolButton {
                        id: takeOut
                        visible: !entry.isCurrent && (rowMouse.containsMouse || hovered || gripMouse.containsMouse || gripMouse.pressed)
                        side: 28
                        iconSize: 14
                        iconName: "x"
                        tip: qsTr("Take out of the queue")
                        onClicked: panel.removeSong(entry.index)
                    }
                }
            }
        }

        UI.Label {
            visible: queue.count === 0
            x: 20
            width: queue.width - 40
            text: qsTr("Nothing is queued. Play an album, a playlist or a song and the rest of it lines up here.")
            size: 13
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
    }
}
