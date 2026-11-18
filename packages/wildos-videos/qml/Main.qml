import QtCore
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import WildOS.Videos
import WildOS.UI as UI

// Wild OS Videos (AppVideos.dc): the title bar with the file's name, the
// picture with its controls, and the other videos in the folder down the
// right. Opened without a video it shows the library of the Videos folder.
// Full screen leaves only the picture; picture in picture shrinks the
// window to the picture, above the other windows.
UI.AppWindow {
    id: window

    readonly property bool watching: Player.path.length > 0
    readonly property bool fullscreen: visibility === Window.FullScreen
    property bool pip: false
    property bool maximisedBefore: false
    property bool maximisedBeforePip: false
    property string errorText
    readonly property bool panelShown: watching && settings.panelOpen && !fullscreen && !pip

    rememberSize: !pip
    width: 1384
    height: 802
    minimumWidth: pip ? 200 : 720
    minimumHeight: pip ? 112 : 440
    title: watching ? Player.fileName : qsTr("Videos")

    function toggleFullscreen() {
        if (pip) {
            return;
        }
        if (fullscreen) {
            leaveFullscreen();
        } else {
            maximisedBefore = visibility === Window.Maximized;
            showFullScreen();
        }
    }
    function leaveFullscreen() {
        if (maximisedBefore) {
            showMaximized();
        } else {
            showNormal();
        }
    }
    function togglePip() {
        if (!watching) {
            return;
        }
        if (pip) {
            pip = false;
            VideosApp.leavePictureInPicture(Qt.size(1384, 802));
            if (maximisedBeforePip) {
                restoreMaximised.start();
            }
            return;
        }
        maximisedBeforePip = visibility === Window.Maximized || (fullscreen && maximisedBefore);
        if (visibility !== Window.Windowed) {
            showNormal();
        }
        const aspect = Player.videoWidth > 0 && Player.videoHeight > 0 ? Player.videoWidth / Player.videoHeight : 16 / 9;
        const size = aspect >= 1 ? Qt.size(400, Math.round(400 / aspect)) : Qt.size(Math.round(320 * aspect), 320);
        pip = true;
        VideosApp.enterPictureInPicture(size);
    }
    function closeVideo() {
        if (!watching || fullscreen || pip) {
            return;
        }
        toast.hide();
        Player.close();
    }
    function say(message) {
        if (message.length > 0) {
            toast.show(message);
        }
    }
    function flash(text) {
        stage.wake();
        stage.flash(text);
    }
    function trackLabel(tracks, id, none) {
        for (const t of tracks) {
            if (t.id === id) {
                return t.label;
            }
        }
        return none;
    }

    Timer {
        id: restoreMaximised
        interval: 400
        onTriggered: window.showMaximized()
    }

    Settings {
        id: settings
        category: "Window"
        property bool panelOpen: true
    }

    Connections {
        target: Player
        function onFailed(message) {
            window.errorText = message;
            window.say(message);
        }
        function onFileChanged() {
            window.errorText = "";
        }
        function onResumed(position) {
            toast.show(qsTr("Picked up at %1").arg(VideosStyle.time(position)), qsTr("Start over"));
        }
    }
    Connections {
        target: toast
        function onActionClicked() {
            Player.seek(0);
            toast.hide();
        }
    }

    Shortcut {
        sequence: "Space"
        enabled: window.watching
        onActivated: {
            Player.playPause();
            stage.wake();
        }
    }
    Shortcut {
        sequence: "Left"
        enabled: window.watching
        onActivated: {
            Player.skip(-10);
            stage.wake();
        }
    }
    Shortcut {
        sequence: "Right"
        enabled: window.watching
        onActivated: {
            Player.skip(10);
            stage.wake();
        }
    }
    Shortcut {
        sequence: "Up"
        enabled: window.watching
        onActivated: {
            Player.stepVolume(1);
            window.flash(qsTr("Volume %1%").arg(Player.volume));
        }
    }
    Shortcut {
        sequence: "Down"
        enabled: window.watching
        onActivated: {
            Player.stepVolume(-1);
            window.flash(qsTr("Volume %1%").arg(Player.volume));
        }
    }
    Shortcut {
        sequence: "M"
        enabled: window.watching
        onActivated: {
            Player.muted = !Player.muted;
            window.flash(Player.muted ? qsTr("Muted") : qsTr("Sound on"));
        }
    }
    Shortcut {
        sequences: ["F", "F11"]
        enabled: window.watching
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Escape"
        enabled: window.watching
        onActivated: {
            if (window.fullscreen) {
                window.leaveFullscreen();
            } else if (window.pip) {
                window.togglePip();
            } else {
                window.closeVideo();
            }
        }
    }
    // Not StandardKey.Back beside "Alt+Left": that is the same key on
    // Linux, and a key listed twice never fires.
    Shortcut {
        sequences: ["Alt+Left", "Back"]
        enabled: !window.fullscreen && !window.pip
        onActivated: {
            if (window.watching) {
                window.closeVideo();
            } else if (!library.atTop) {
                library.folder = Library.parentOf(library.folder);
            }
        }
    }
    Shortcut {
        sequence: "["
        enabled: window.watching
        onActivated: {
            Player.stepSpeed(-1);
            window.flash(qsTr("Speed %1").arg(VideosStyle.speed(Player.speed)));
        }
    }
    Shortcut {
        sequence: "]"
        enabled: window.watching
        onActivated: {
            Player.stepSpeed(1);
            window.flash(qsTr("Speed %1").arg(VideosStyle.speed(Player.speed)));
        }
    }
    Shortcut {
        sequence: "C"
        enabled: window.watching
        onActivated: {
            if (Player.subtitleTracks.length === 0) {
                window.flash(qsTr("No subtitles"));
                return;
            }
            Player.cycleSubtitles();
            subtitleNotice.start();
        }
    }
    // The track changes once mpv has switched, a moment later.
    Timer {
        id: subtitleNotice
        interval: 150
        onTriggered: window.flash(Player.subtitleTrack > 0 ? window.trackLabel(Player.subtitleTracks, Player.subtitleTrack, "") : qsTr("Subtitles off"))
    }
    Shortcut {
        sequence: "N"
        enabled: window.watching && Player.canNext
        onActivated: Player.next()
    }
    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: openDialog.open()
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (drop.hasUrls && drop.urls.length > 0) {
                const paths = [];
                for (let i = 0; i < drop.urls.length; ++i) {
                    paths.push(Library.pathFromUrl(drop.urls[i]));
                }
                Player.openFiles(paths);
                drop.accept();
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            visible: !window.fullscreen && !window.pip

            Rectangle {
                anchors.fill: parent
                topLeftRadius: window.maximised ? 0 : UI.Style.windowRadius - 1
                topRightRadius: topLeftRadius
                color: VideosStyle.titleShade
            }

            UI.TitleBar {
                anchors.fill: parent
                tall: true
                frame: window.frame
                showTitle: false

                // The title bar puts its content 16px in; the board's icon is 14.
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: window.watching ? -8 : -2
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    PanelButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: window.watching
                        iconName: "back"
                        tip: qsTr("Back to your videos")
                        onClicked: window.closeVideo()
                    }
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        source: "qrc:/qt/qml/WildOS/Videos/data/wildos-videos.svg"
                        sourceSize.width: 40
                        sourceSize.height: 40
                    }
                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - 170)
                        visible: window.watching
                        text: Player.fileName
                        font.weight: UI.Style.weightMedium
                        color: window.active ? UI.Style.ink : UI.Style.inkMuted
                    }
                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Videos")
                        font.weight: window.watching ? UI.Style.weightRegular : UI.Style.weightMedium
                        color: window.watching ? UI.Style.inkSubtle : window.active ? UI.Style.ink : UI.Style.inkMuted
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Stage {
                    id: stage
                    anchors.fill: parent
                    visible: window.watching
                    cornerRadius: window.maximised ? 0 : UI.Style.windowRadius - 1
                    roundTop: window.pip
                    roundBottomRight: !window.panelShown
                    fullscreen: window.fullscreen
                    pip: window.pip
                    panelHidden: !settings.panelOpen
                    menuOpen: pictureMenu.opened || itemMenu.opened
                    errorText: window.errorText
                    onFullscreenRequested: window.toggleFullscreen()
                    onPipRequested: window.togglePip()
                    onPanelRequested: settings.panelOpen = true
                    onMoveRequested: window.startSystemMove()
                    onSpeedMenuRequested: button => pictureMenu.show(button, window.speedEntries())
                    onVolumeStepRequested: steps => {
                        Player.stepVolume(steps);
                        window.flash(qsTr("Volume %1%").arg(Player.volume));
                    }
                    onMenuRequested: (x, y) => {
                        itemMenu.entries = [
                            { text: Player.playing ? qsTr("Pause") : qsTr("Play"), icon: Player.playing ? "media-pause" : "play", enabled: Player.loaded, run: () => Player.playPause() },
                            { text: window.fullscreen ? qsTr("Leave full screen") : qsTr("Full screen"), icon: "fullscreen", shortcut: "F11", run: () => window.toggleFullscreen() },
                            { text: qsTr("Picture in picture"), enabled: Player.loaded, run: () => window.togglePip() },
                            { separator: true },
                            { text: qsTr("Speed"), submenu: window.speedEntries() },
                            { text: qsTr("Audio"), icon: "sound", submenu: window.audioEntries() },
                            { text: qsTr("Subtitles"), icon: "list", submenu: window.subtitleEntries() },
                            { text: qsTr("Shape of picture"), icon: "resolution", submenu: window.aspectEntries() },
                            { separator: true },
                            { text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(Player.path) }
                        ];
                        itemMenu.popup(stage, x, y);
                    }
                    onSubtitlesMenuRequested: button => pictureMenu.show(button, window.subtitleEntries())
                    onSettingsMenuRequested: button => {
                        pictureMenu.show(button, [
                            { text: qsTr("Audio"), icon: "sound", submenu: window.audioEntries() },
                            { text: qsTr("Subtitles"), icon: "list", submenu: window.subtitleEntries() },
                            { text: qsTr("Shape of picture"), icon: "resolution", submenu: window.aspectEntries() },
                            { separator: true },
                            { text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(Player.path) }
                        ]);
                    }
                }

                LibraryPage {
                    id: library
                    anchors.fill: parent
                    visible: !window.watching
                    onOpenRequested: openDialog.open()
                    onMenuRequested: (path, item, x, y) => window.videoMenu(path, item, x, y)
                }
            }

            FolderPanel {
                visible: window.panelShown
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                onHideRequested: settings.panelOpen = false
                onMenuRequested: (path, item, x, y) => window.videoMenu(path, item, x, y)
            }
        }
    }

    function speedEntries() {
        const entries = [];
        for (const s of [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]) {
            entries.push({ text: s === 1 ? qsTr("Normal") : VideosStyle.speed(s), radio: true, checked: Math.abs(Player.speed - s) < 0.01, run: () => Player.speed = s });
        }
        return entries;
    }
    function audioEntries() {
        if (Player.audioTracks.length === 0) {
            return [{ text: qsTr("No sound in this video"), enabled: false }];
        }
        return Player.audioTracks.map(t => ({ text: t.label, radio: true, checked: t.id === Player.audioTrack, run: () => Player.setAudioTrack(t.id) }));
    }
    function subtitleEntries() {
        const entries = [{ text: qsTr("Off"), radio: true, checked: Player.subtitleTrack === 0, run: () => Player.setSubtitleTrack(0) }];
        for (const t of Player.subtitleTracks) {
            entries.push({ text: t.label, radio: true, checked: t.id === Player.subtitleTrack, run: () => Player.setSubtitleTrack(t.id) });
        }
        if (Player.subtitleTracks.length === 0) {
            entries.push({ separator: true });
            entries.push({ text: qsTr("No subtitles in this video, or beside it"), enabled: false });
        }
        return entries;
    }
    function aspectEntries() {
        const shapes = [["", qsTr("As the video is")], ["16:9", "16:9"], ["4:3", "4:3"], ["21:9", "21:9"]];
        return shapes.map(s => ({ text: s[1], radio: true, checked: Player.aspect === s[0], run: () => Player.aspect = s[0] }));
    }
    function videoMenu(path, item, x, y) {
        const watched = Positions.watched(path);
        itemMenu.entries = [
            { text: path === Player.path ? qsTr("Playing") : qsTr("Play"), icon: "play", enabled: path !== Player.path, run: () => Player.open(path) },
            { text: qsTr("Show in Files"), icon: "open-folder", run: () => Library.showInFiles(path) },
            { separator: true },
            watched || Positions.progress(path) > 0
                ? { text: qsTr("Start from the beginning next time"), icon: "refresh", run: () => Positions.setWatched(path, false) }
                : { text: qsTr("Mark as watched"), icon: "check", run: () => Positions.setWatched(path, true) }
        ];
        itemMenu.popup(item, x, y);
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: window.panelShown ? -165 : 0
        anchors.bottom: parent.bottom
        anchors.bottomMargin: window.pip ? 60 : 110
    }

    // The menus off the controls open above them, right-aligned to the
    // button, since the controls are at the foot of the picture.
    UI.Menu {
        id: pictureMenu
        function show(button, list) {
            entries = list;
            parent = button;
            open();
        }
        x: parent ? parent.width - width : 0
        y: -height - 8
    }

    UI.Menu {
        id: itemMenu
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open a video")
        fileMode: FileDialog.OpenFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.MoviesLocation)
        nameFilters: [qsTr("Videos (*.mp4 *.m4v *.mkv *.webm *.mov *.avi *.wmv *.flv *.mpg *.mpeg *.ts *.m2ts *.mts *.3gp *.ogv)"), qsTr("All files (*)")]
        onAccepted: Player.open(Library.pathFromUrl(selectedFile))
    }
}
