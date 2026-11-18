import QtQuick
import QtQuick.Templates as T

import org.kde.kirigami as Kirigami
import org.kde.plasma.private.mpris as Mpris
import WildOS.UI as UI

import "components"

// What is playing, in a glass pill the width of the password field, with
// previous, play or pause, and next.
Item {
    id: media

    property Item backdrop: null

    visible: players.count > 0
    implicitWidth: 300
    implicitHeight: visible ? 56 : 0

    Repeater {
        id: players
        model: Mpris.MultiplexerModel { }

        Item {
            id: player

            required property var model

            anchors.fill: parent
            enabled: model.canControl

            Glass {
                anchors.fill: parent
                backdrop: media.backdrop
                tint: UI.Style.glassOnWallpaperField
                radius: 18
                blurRadius: 64
            }

            Image {
                id: art
                anchors.left: parent.left
                anchors.leftMargin: 8
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
                anchors.left: art.visible ? art.right : parent.left
                anchors.leftMargin: art.visible ? 10 : 16
                anchors.right: controls.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                UI.Label {
                    width: parent.width
                    text: player.model.track.length > 0 ? player.model.track
                        : player.model.playbackStatus > Mpris.PlaybackStatus.Stopped ? qsTr("Untitled") : qsTr("Nothing playing")
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkOnWallpaper
                }
                UI.Label {
                    width: parent.width
                    text: player.model.artist || player.model.identity
                    size: 12
                    color: UI.Style.inkOnWallpaperMuted
                }
            }

            Row {
                id: controls
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                MediaButton {
                    visible: player.model.canGoPrevious || player.model.canGoNext
                    enabled: player.model.canGoPrevious
                    iconName: LayoutMirroring.enabled ? "media-skip-forward" : "media-skip-backward"
                    label: qsTr("Previous track")
                    onClicked: player.model.container.Previous()
                }
                MediaButton {
                    iconName: player.model.playbackStatus === Mpris.PlaybackStatus.Playing ? "media-playback-pause" : "media-playback-start"
                    label: player.model.playbackStatus === Mpris.PlaybackStatus.Playing ? qsTr("Pause") : qsTr("Play")
                    onClicked: player.model.container.PlayPause()
                }
                MediaButton {
                    visible: player.model.canGoPrevious || player.model.canGoNext
                    enabled: player.model.canGoNext
                    iconName: LayoutMirroring.enabled ? "media-skip-backward" : "media-skip-forward"
                    label: qsTr("Next track")
                    onClicked: player.model.container.Next()
                }
            }
        }
    }

    component MediaButton: T.AbstractButton {
        id: button

        property string iconName
        property string label

        width: 32
        height: 32
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        opacity: enabled ? 1 : 0.5
        Accessible.name: label
        Accessible.role: Accessible.Button

        background: Rectangle {
            radius: height / 2
            color: button.down ? UI.Style.glassOnWallpaperPressed : button.hovered ? UI.Style.glassOnWallpaperHover : "transparent"

            UI.FocusRing {
                visible: button.visualFocus
                targetRadius: parent.radius
            }
        }

        contentItem: Item {
            Kirigami.Icon {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: button.iconName
                color: UI.Style.inkOnWallpaper
                isMask: true
            }
        }
    }
}
