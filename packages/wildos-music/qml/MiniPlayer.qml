import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import WildOS.Music
import WildOS.UI as UI

// The mini player: a small window that stays above the others with the
// cover, the song, the transport controls and the seek bar. Drag it by
// anywhere that is not a control; the button at the top right brings the
// full window back.
Window {
    id: mini

    readonly property var song: Player.current
    readonly property bool hasSong: song.path !== undefined

    width: 380
    height: 132
    minimumWidth: 320
    minimumHeight: 132
    maximumHeight: 132
    // KWin finds the window by this title to keep it on top (MusicApp).
    title: qsTr("Music mini player")
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    onActiveChanged: if (active) frame.recheck()

    UI.WindowFrame {
        id: frame
        window: mini
        radius: UI.Style.radiusMd
        shadowColor: UI.Style.shadowWindow
    }

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusMd
        color: frame.blurAvailable ? UI.Style.glassWindow : UI.Style.windowOpaque
        border.width: 1
        border.color: UI.Style.windowEdge
    }

    MouseArea {
        anchors.fill: parent
        onPressed: mini.startSystemMove()
    }

    Shortcut {
        sequence: "Space"
        onActivated: Player.playPause()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: MusicApp.showMain()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 14

        Cover {
            Layout.preferredWidth: 104
            Layout.preferredHeight: 104
            radius: 8
            source: mini.song.cover ?? ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: 2

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 2

                    UI.Label {
                        Layout.fillWidth: true
                        text: mini.hasSong ? mini.song.title : qsTr("Nothing playing")
                        size: 14
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        visible: mini.hasSong
                        text: mini.song.artist ?? ""
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                }
                ToolButton {
                    Layout.alignment: Qt.AlignTop
                    side: 28
                    iconSize: 14
                    iconName: "expand"
                    tip: qsTr("Back to Music")
                    onClicked: MusicApp.showMain()
                }
                ToolButton {
                    Layout.alignment: Qt.AlignTop
                    side: 28
                    iconSize: 14
                    iconName: "x"
                    tip: qsTr("Close Music")
                    onClicked: Qt.quit()
                }
            }

            Item {
                Layout.fillHeight: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                ToolButton {
                    side: 32
                    iconName: "shuffle"
                    lit: Player.shuffle
                    tip: Player.shuffle ? qsTr("Shuffle is on") : qsTr("Shuffle")
                    onClicked: Player.shuffle = !Player.shuffle
                }
                ToolButton {
                    side: 32
                    iconName: "previous"
                    iconSize: 18
                    enabled: Player.canPrevious
                    tip: qsTr("Previous")
                    onClicked: Player.previous()
                }
                PlayButton {
                    side: 36
                    enabled: Player.queue.length > 0
                }
                ToolButton {
                    side: 32
                    iconName: "next"
                    iconSize: 18
                    enabled: Player.canNext
                    tip: qsTr("Next")
                    onClicked: Player.next()
                }
                HeartButton {
                    side: 32
                    iconSize: 16
                    paths: mini.hasSong ? [mini.song.path] : []
                    restColor: UI.Style.iconInk
                }
            }

            SeekBar {
                Layout.fillWidth: true
                Layout.topMargin: 6
                from: 0
                to: Math.max(1, Player.duration)
                source: Player.position
                knob: false
                enabled: mini.hasSong && Player.seekable
                accessibleName: qsTr("Position in the song")
                onCommitted: value => Player.seek(value)
            }
        }
    }
}
