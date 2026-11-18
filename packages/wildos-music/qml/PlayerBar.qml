import QtQuick
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// The player across the foot of the window (AppMusic.dc): the song with
// its cover and heart on the left; shuffle, previous, the round play
// button, next and repeat over the seek bar in the middle; lyrics (when
// the song has them), the queue, volume and the mini player on the right.
Rectangle {
    id: bar

    property string panel

    signal openAlbum(string key)
    signal openArtist(string name)
    signal panelRequested(string name)

    readonly property var song: Player.current
    readonly property bool hasSong: song.path !== undefined

    implicitHeight: 84
    color: MusicStyle.barShade

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: UI.Style.borderSubtle
    }

    Item {
        id: nowPlaying
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(300, (bar.width - 36 - 32) * 0.3)
        height: 52

        RowLayout {
            anchors.fill: parent
            spacing: 12
            visible: bar.hasSong

            Cover {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 52
                source: bar.song.cover ?? ""
                radius: 6

                MouseArea {
                    anchors.fill: parent
                    cursorShape: (bar.song.albumKey ?? "").length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if ((bar.song.albumKey ?? "").length > 0) {
                            bar.openAlbum(bar.song.albumKey);
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: bar.song.title ?? ""
                    size: 13.5
                    font.weight: UI.Style.weightSemiBold
                    font.underline: titleMouse.containsMouse && (bar.song.albumKey ?? "").length > 0

                    MouseArea {
                        id: titleMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: (bar.song.albumKey ?? "").length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if ((bar.song.albumKey ?? "").length > 0) {
                                bar.openAlbum(bar.song.albumKey);
                            }
                        }
                    }
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: bar.song.artist ?? ""
                    size: 12.5
                    color: UI.Style.inkMuted
                    font.underline: artistMouse.containsMouse

                    MouseArea {
                        id: artistMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bar.openArtist(bar.song.albumArtist || bar.song.artist)
                    }
                }
            }
            HeartButton {
                side: 30
                iconSize: 16
                paths: bar.hasSong ? [bar.song.path] : []
                restColor: UI.Style.iconInk
            }
        }
    }

    ColumnLayout {
        id: centre
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(560, bar.width - nowPlaying.width * 2 - 36 - 32)
        spacing: 8

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 14

            ToolButton {
                iconName: "shuffle"
                lit: Player.shuffle
                tip: Player.shuffle ? qsTr("Shuffle is on") : qsTr("Shuffle")
                onClicked: Player.shuffle = !Player.shuffle
                Accessible.checkable: true
                Accessible.checked: Player.shuffle
            }
            ToolButton {
                iconName: "previous"
                iconSize: 18
                enabled: Player.canPrevious
                tip: qsTr("Previous")
                onClicked: Player.previous()
            }
            PlayButton {
                enabled: Player.queue.length > 0
            }
            ToolButton {
                iconName: "next"
                iconSize: 18
                enabled: Player.canNext
                tip: qsTr("Next")
                onClicked: Player.next()
            }
            ToolButton {
                iconName: Player.repeat === "one" ? "repeat-one" : "repeat"
                lit: Player.repeat !== "off"
                tip: Player.repeat === "one" ? qsTr("Repeating this song") : Player.repeat === "all" ? qsTr("Repeating all") : qsTr("Repeat")
                onClicked: Player.cycleRepeat()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            UI.Label {
                Layout.preferredWidth: 34
                horizontalAlignment: Text.AlignRight
                text: MusicStyle.time(seek.pressed ? seek.value : Player.position)
                size: 11.5
                color: UI.Style.inkMuted
            }
            SeekBar {
                id: seek
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                Layout.alignment: Qt.AlignHCenter
                from: 0
                to: Math.max(1, Player.duration)
                source: Player.position
                enabled: bar.hasSong && Player.seekable
                timeTip: true
                accessibleName: qsTr("Position in the song")
                onCommitted: value => Player.seek(value)
            }
            UI.Label {
                Layout.preferredWidth: 34
                text: bar.hasSong ? MusicStyle.time(Player.duration) : "0:00"
                size: 11.5
                color: UI.Style.inkMuted
            }
        }
    }

    RowLayout {
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        ToolButton {
            visible: bar.song.lyrics === true
            iconName: "artist"
            lit: bar.panel === "lyrics"
            tip: qsTr("Lyrics")
            onClicked: bar.panelRequested("lyrics")
        }
        ToolButton {
            iconName: "playlist"
            lit: bar.panel === "queue"
            tip: qsTr("Queue")
            onClicked: bar.panelRequested("queue")
        }
        ToolButton {
            iconName: Player.muted || Player.volume === 0 ? "volume-off" : Player.volume < 0.5 ? "volume-low" : "volume"
            tip: Player.muted ? qsTr("Unmute") : qsTr("Mute")
            onClicked: Player.muted = !Player.muted
        }
        SeekBar {
            Layout.preferredWidth: 90
            from: 0
            to: 1
            knob: false
            source: Player.muted ? 0 : Player.volume
            accessibleName: qsTr("Volume")
            onMoved: {
                Player.muted = false;
                Player.volume = value;
            }
            onCommitted: value => {
                Player.muted = false;
                Player.volume = value;
            }

            // A touchpad sends many small turns; a step is a full notch.
            WheelHandler {
                property real turned: 0
                onWheel: event => {
                    turned += event.angleDelta.y;
                    const steps = Math.trunc(turned / 120);
                    if (steps !== 0) {
                        turned -= steps * 120;
                        Player.stepVolume(steps);
                    }
                }
            }
        }
        ToolButton {
            iconName: "mini"
            tip: qsTr("Mini player")
            onClicked: MusicApp.showMini()
        }
    }
}
