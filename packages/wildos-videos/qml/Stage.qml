import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import WildOS.Videos
import WildOS.UI as UI

// The picture and everything over it (AppVideos.dc): the title and where
// the file is at the top left, and along the foot the seek bar with its
// chapters, then play, back and forward 10 seconds, the volume and the
// time on the left, and speed, subtitles, settings, picture in picture and
// full screen on the right. The controls fade after three seconds without
// the pointer moving while the video plays, and come back when it moves.
// All of it stays dark in light mode.
Item {
    id: stage

    // Which corners meet the window's own rounded corners.
    property real cornerRadius: 0
    property bool roundTop: false
    property bool roundBottomRight: false
    property bool fullscreen: false
    property bool pip: false
    // A menu from the controls is open: the controls stay while it is.
    property bool menuOpen: false
    // The side panel is hidden: a button over the picture brings it back.
    property bool panelHidden: false
    // Why the video is not playing, if it could not be opened.
    property string errorText

    readonly property bool chromeShown: !Player.playing || pointerRecently || menuOpen || controlsHovered.hovered || !Player.loaded
    property bool pointerRecently: true

    signal fullscreenRequested()
    signal pipRequested()
    signal speedMenuRequested(Item button)
    signal subtitlesMenuRequested(Item button)
    signal settingsMenuRequested(Item button)
    signal moveRequested()
    signal panelRequested()
    signal menuRequested(real x, real y)
    signal volumeStepRequested(int steps)

    function wake() {
        pointerRecently = true;
        idle.restart();
    }
    // A short note in the middle of the picture for a change made from the
    // keyboard, where no control shows it: "Volume 80%", "Speed 1.5×".
    function flash(text) {
        notice.text = text;
        notice.opacity = 1;
        noticeTimer.restart();
    }

    Timer {
        id: noticeTimer
        interval: 1200
        onTriggered: notice.opacity = 0
    }

    Timer {
        id: idle
        interval: 3000
        onTriggered: stage.pointerRecently = false
    }
    Connections {
        target: Player
        function onPlayingChanged() {
            stage.wake();
        }
        function onFileChanged() {
            stage.wake();
        }
    }

    Item {
        id: picture
        anchors.fill: parent
        layer.enabled: stage.cornerRadius > 0
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: corners
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        Rectangle {
            anchors.fill: parent
            color: VideosStyle.picture
        }

        VideoOutput {
            anchors.fill: parent
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 90
            visible: !stage.pip
            opacity: stage.chromeShown ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: UI.Style.motionNormal }
            }
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.55) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0) }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: stage.pip ? 100 : 150
            opacity: stage.chromeShown ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: UI.Style.motionNormal }
            }
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.75) }
            }
        }
    }

    Rectangle {
        id: corners
        anchors.fill: parent
        visible: false
        layer.enabled: true
        layer.smooth: true
        topLeftRadius: stage.roundTop ? stage.cornerRadius : 0
        topRightRadius: stage.roundTop ? stage.cornerRadius : 0
        bottomLeftRadius: stage.cornerRadius
        bottomRightRadius: stage.roundBottomRight ? stage.cornerRadius : 0
    }

    MouseArea {
        id: surface
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: stage.chromeShown ? Qt.ArrowCursor : Qt.BlankCursor
        property point origin
        property bool moved: false
        property real turned: 0
        onPositionChanged: event => {
            stage.wake();
            if (pressedButtons === Qt.LeftButton && stage.pip && !moved && Math.abs(event.x - origin.x) + Math.abs(event.y - origin.y) > 4) {
                moved = true;
                stage.moveRequested();
            }
        }
        onPressed: event => {
            origin = Qt.point(event.x, event.y);
            moved = false;
        }
        onClicked: event => {
            if (event.button === Qt.RightButton) {
                if (!stage.pip) {
                    stage.menuRequested(event.x, event.y);
                }
            } else if (!moved) {
                clickTimer.restart();
            }
        }
        onDoubleClicked: event => {
            if (event.button !== Qt.LeftButton) {
                return;
            }
            clickTimer.stop();
            if (!stage.pip) {
                stage.fullscreenRequested();
            }
        }
        // A touchpad sends many small turns; a step is a full notch.
        onWheel: event => {
            turned += event.angleDelta.y;
            const steps = Math.trunc(turned / 120);
            if (steps !== 0) {
                turned -= steps * 120;
                stage.wake();
                stage.volumeStepRequested(steps);
            }
        }

        // A double click is for full screen; one click waits to be sure it
        // is not the start of one before it plays or pauses.
        Timer {
            id: clickTimer
            interval: 220
            onTriggered: Player.playPause()
        }
    }

    UI.Label {
        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 48)
        visible: stage.errorText.length > 0 && !Player.loaded
        text: stage.errorText
        size: 14
        color: VideosStyle.overPictureMuted
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    Rectangle {
        id: noticeBox
        anchors.centerIn: parent
        width: notice.implicitWidth + 32
        height: notice.implicitHeight + 18
        radius: UI.Style.radiusMd
        color: VideosStyle.tipGlass
        border.width: 1
        border.color: VideosStyle.tipEdge
        opacity: notice.opacity
        visible: opacity > 0

        UI.Label {
            id: notice
            anchors.centerIn: parent
            size: stage.pip ? 13 : 16
            font.weight: UI.Style.weightMedium
            color: VideosStyle.overPicture
            opacity: 0
            Behavior on opacity {
                NumberAnimation { duration: UI.Style.motionNormal }
            }
        }
    }

    PictureButton {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 14
        anchors.topMargin: 14
        visible: stage.panelHidden && !stage.pip && !stage.fullscreen && opacity > 0
        opacity: stage.chromeShown ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionNormal }
        }
        iconName: "panel-open"
        iconSize: 16
        tip: qsTr("Show the list")
        onClicked: stage.panelRequested()
    }

    ColumnLayout {
        id: heading
        visible: !stage.pip && Player.loaded
        x: 22
        y: 18
        width: Math.min(parent.width - 44, implicitWidth)
        spacing: 3
        opacity: stage.chromeShown ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionNormal }
        }

        UI.Label {
            Layout.fillWidth: true
            text: Player.title
            size: 16
            font.weight: UI.Style.weightSemiBold
            color: VideosStyle.overPicture
            style: Text.Raised
            styleColor: Qt.rgba(0, 0, 0, 0.25)
        }
        UI.Label {
            Layout.fillWidth: true
            text: [Player.place, Player.quality].filter(s => s.length > 0).join(" · ")
            size: 12.5
            color: VideosStyle.overPictureMuted
            style: Text.Raised
            styleColor: Qt.rgba(0, 0, 0, 0.25)
        }
    }

    ColumnLayout {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: stage.pip ? 12 : 22
        anchors.rightMargin: stage.pip ? 12 : 22
        anchors.bottomMargin: stage.pip ? 8 : 18
        spacing: stage.pip ? 4 : 10
        opacity: stage.chromeShown ? 1 : 0
        visible: opacity > 0 && Player.loaded
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionNormal }
        }

        HoverHandler {
            id: controlsHovered
        }

        SeekBar {
            Layout.fillWidth: true
            position: Player.position
            duration: Player.duration
            chapters: Player.chapters
            onSeekRequested: seconds => Player.seek(seconds)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            PictureButton {
                iconName: Player.playing ? "pause" : "play"
                iconSize: 20
                tip: Player.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: Player.playPause()
            }
            PictureButton {
                iconName: "back-10"
                tip: qsTr("Back 10 seconds")
                onClicked: Player.skip(-10)
            }
            PictureButton {
                iconName: "forward-10"
                tip: qsTr("Forward 10 seconds")
                onClicked: Player.skip(10)
            }
            PictureButton {
                visible: !stage.pip
                iconName: Player.muted || Player.volume === 0 ? "volume-off" : Player.volume < 50 ? "volume-low" : "volume"
                tip: Player.muted ? qsTr("Sound on") : qsTr("Mute")
                onClicked: Player.muted = !Player.muted
            }
            VolumeBar {
                visible: !stage.pip
                value: Player.volume
                onMoved: {
                    Player.muted = false;
                    Player.volume = Math.round(value);
                }
            }
            Row {
                visible: !stage.pip
                Layout.leftMargin: 12
                spacing: 4

                UI.Label {
                    size: 13
                    color: VideosStyle.overPicture
                    text: VideosStyle.time(Player.position)
                }
                UI.Label {
                    size: 13
                    color: VideosStyle.overPictureFaint
                    text: "/ " + VideosStyle.time(Player.duration)
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Rectangle {
                id: speedChip
                visible: !stage.pip
                Layout.rightMargin: 4
                implicitWidth: speedLabel.implicitWidth + 18
                implicitHeight: 26
                radius: UI.Style.radiusXs
                color: speedMouse.pressed ? VideosStyle.overPicturePressed : speedMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : VideosStyle.overPictureChip
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Speed, %1").arg(speedLabel.text)

                UI.Label {
                    id: speedLabel
                    anchors.centerIn: parent
                    size: 12.5
                    color: VideosStyle.overPicture
                    text: VideosStyle.speed(Player.speed)
                }
                MouseArea {
                    id: speedMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: stage.speedMenuRequested(speedChip)
                }
                PictureTip {
                    text: qsTr("Speed")
                    visible: speedMouse.containsMouse && !speedMouse.pressed && !stage.menuOpen
                }
            }
            PictureButton {
                id: subtitlesButton
                tipAllowed: !stage.menuOpen
                visible: !stage.pip
                iconName: "subtitles"
                lit: Player.subtitleTrack > 0
                tip: qsTr("Subtitles")
                onClicked: stage.subtitlesMenuRequested(subtitlesButton)
            }
            PictureButton {
                id: settingsButton
                tipAllowed: !stage.menuOpen
                visible: !stage.pip
                iconName: "settings"
                tip: qsTr("Settings")
                onClicked: stage.settingsMenuRequested(settingsButton)
            }
            PictureButton {
                iconName: stage.pip ? "pip-exit" : "pip"
                tip: stage.pip ? qsTr("Back to Videos") : qsTr("Picture in picture")
                onClicked: stage.pipRequested()
            }
            PictureButton {
                visible: !stage.pip
                iconName: stage.fullscreen ? "fullscreen-exit" : "fullscreen"
                tip: stage.fullscreen ? qsTr("Leave full screen") : qsTr("Full screen")
                onClicked: stage.fullscreenRequested()
            }
        }
    }
}
