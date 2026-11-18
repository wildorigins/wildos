pragma ComponentBehavior: Bound

import QtQuick
import org.kde.plasma.private.mpris as Mpris
import WildOS.UI as UI

// What is playing, above the tiles as Windows shows it: the cover, the
// track and who it is by, with previous, play or pause, and next. It follows
// the player Plasma treats as the current one, the same one the media keys
// and the lock screen control, and is not there when nothing is open that
// plays.
Item {
    id: media

    readonly property bool available: players.count > 0

    visible: available
    implicitHeight: 64

    Repeater {
        id: players
        model: Mpris.MultiplexerModel {}

        Rectangle {
            id: player

            required property var model
            readonly property bool playing: model.playbackStatus === Mpris.PlaybackStatus.Playing

            anchors.fill: parent
            radius: Metrics.tileRadius
            color: UI.Style.surfaceTile
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Now playing")

            Image {
                id: art
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                height: 40
                visible: status === Image.Ready
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                source: player.model.artUrl
                sourceSize: Qt.size(80, 80)
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: art.visible ? 64 : 14
                anchors.right: controls.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                UI.Label {
                    width: parent.width
                    text: player.model.track.length > 0 ? player.model.track
                        : player.model.playbackStatus > Mpris.PlaybackStatus.Stopped ? qsTr("Untitled") : qsTr("Nothing playing")
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    width: parent.width
                    visible: text.length > 0
                    text: player.model.artist || player.model.identity || ""
                    size: 12
                    color: UI.Style.inkMuted
                }
            }

            Row {
                id: controls
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                enabled: player.model.canControl

                UI.IconButton {
                    visible: player.model.canGoPrevious || player.model.canGoNext
                    enabled: player.model.canGoPrevious
                    width: 32
                    height: 32
                    glyph: Icons.skipPrevious
                    tip: qsTr("Previous track")
                    onClicked: player.model.container.Previous()
                }

                UI.IconButton {
                    width: 32
                    height: 32
                    glyph: player.playing ? Icons.pause : Icons.play
                    tip: player.playing ? qsTr("Pause") : qsTr("Play")
                    onClicked: player.model.container.PlayPause()
                }

                UI.IconButton {
                    visible: player.model.canGoPrevious || player.model.canGoNext
                    enabled: player.model.canGoNext
                    width: 32
                    height: 32
                    glyph: Icons.skipNext
                    tip: qsTr("Next track")
                    onClicked: player.model.container.Next()
                }
            }
        }
    }
}
