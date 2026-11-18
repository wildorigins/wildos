import QtQuick
import QtQuick.Layouts
import WildOS.Voice
import WildOS.UI as UI

// Wild OS Voice (AppVoice.dc): the title bar, the list of recordings on the
// left and the recording, player or Record button on the right.
UI.AppWindow {
    id: window

    // The finished recording picked in the list, or one opened from Files.
    property string selectedPath
    property var selected: ({})
    property var bars: []
    // Closed while a transcript was still being finished: hidden until it is.
    property bool closing: false
    // What the button on the note at the foot of the window does.
    property var toastAction: null

    readonly property string mode: Recorder.active && (selectedPath.length === 0 || selectedPath === Recorder.path) ? "record"
        : selectedPath.length > 0 && selected.path !== undefined ? "play" : "idle"

    width: 1384
    height: 802
    minimumWidth: 900
    minimumHeight: 620
    title: qsTr("Voice")

    function say(message, action, run) {
        if (message.length > 0) {
            toastAction = run ?? null;
            toast.show(message, action);
        }
    }
    function pick(path) {
        selectedPath = path;
        refreshSelected();
    }
    function refreshSelected() {
        if (selectedPath.length === 0 || (Recorder.active && selectedPath === Recorder.path)) {
            selected = ({});
            bars = [];
            return;
        }
        const info = Recordings.info(selectedPath);
        if (info.path === undefined) {
            selectedPath = "";
            selected = ({});
            bars = [];
            return;
        }
        const changed = selected.path !== info.path;
        selected = info;
        if (changed) {
            bars = [];
            Recordings.loadWaveform(info.path);
        }
    }
    function startRecording() {
        Player.pause();
        selectedPath = "";
        refreshSelected();
        Recorder.start();
    }
    function recordingMenu(recording, row, x, y) {
        const live = Recorder.active && recording.path === Recorder.path;
        menu.entries = [
            { text: qsTr("Rename"), icon: "rename", enabled: !live, run: () => { pick(recording.path); renameDialog.ask(); } },
            { text: qsTr("Show in Files"), icon: "open-folder", run: () => Recordings.showInFiles(recording.path) },
            { separator: true },
            { text: qsTr("Delete"), icon: "delete", danger: true, enabled: !live, run: () => { pick(recording.path); deleteDialog.open(); } }
        ];
        menu.popup(row, x, y);
    }

    onModeChanged: {
        Player.load(mode === "play" ? selected.path : "");
        Transcripts.shownPath = mode === "record" ? Recorder.path : mode === "play" ? selected.path : "";
    }
    onSelectedChanged: {
        if (mode === "play") {
            Player.load(selected.path);
            Transcripts.shownPath = selected.path;
        }
    }
    onClosing: close => {
        // A recording is finished properly on the way out; a transcript
        // still writing down its last seconds gets to finish, out of sight.
        if (Recorder.active) {
            Recorder.stop();
        }
        if (Recorder.active || Transcripts.workingPath.length > 0) {
            close.accepted = false;
            closing = true;
            window.hide();
        }
    }

    Connections {
        target: Recordings
        function onItemsChanged() {
            window.refreshSelected();
        }
        function onWaveformReady(path, bars, duration) {
            if (path === window.selected.path) {
                window.bars = bars;
            }
        }
        function onRenamed(from, to) {
            if (window.selectedPath === from) {
                window.pick(to);
            }
        }
        function onRemoved(path) {
            if (window.selectedPath === path) {
                window.pick("");
            }
        }
        function onLastErrorChanged() {
            window.say(Recordings.lastError);
        }
    }
    Connections {
        target: Recorder
        function onStarted(path) {
            window.selectedPath = "";
            Transcripts.shownPath = path;
        }
        function onFinished(path) {
            Recordings.forgetWaveform(path);
            if (!window.closing && path.length > 0) {
                window.pick(path);
            }
            if (window.closing && Transcripts.workingPath.length === 0) {
                Qt.quit();
            }
        }
        function onFailed(message) {
            window.say(message);
        }
    }
    Connections {
        target: Transcripts
        function onWorkingChanged() {
            if (window.closing && !Recorder.active && Transcripts.workingPath.length === 0) {
                Qt.quit();
            }
        }
    }
    Connections {
        target: Player
        function onFailed(message) {
            window.say(message);
        }
    }
    Connections {
        target: toast
        function onActionClicked() {
            const run = window.toastAction;
            window.toastAction = null;
            if (run) {
                run();
            }
        }
    }
    Connections {
        target: VoiceApp
        function onPendingPathChanged() {
            if (VoiceApp.pendingPath.length > 0) {
                window.pick(VoiceApp.pendingPath);
                VoiceApp.takePendingPath();
            }
        }
    }
    Component.onCompleted: {
        if (VoiceApp.pendingPath.length > 0) {
            pick(VoiceApp.pendingPath);
            VoiceApp.takePendingPath();
        }
    }

    Shortcut {
        sequence: "Space"
        onActivated: {
            if (window.mode === "play") {
                Player.playPause();
            } else if (Recorder.state === "recording") {
                Recorder.pause();
            } else if (Recorder.state === "paused") {
                Recorder.resume();
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: {
            if (Recorder.active) {
                Recorder.stop();
            } else {
                window.startRecording();
            }
        }
    }
    Shortcut {
        sequence: "M"
        enabled: Recorder.active
        onActivated: Recorder.mark()
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: sidebar.startSearch()
    }
    Shortcut {
        sequence: "F2"
        enabled: window.mode === "play"
        onActivated: renameDialog.ask()
    }
    Shortcut {
        sequences: [StandardKey.Delete]
        enabled: window.mode === "play"
        onActivated: deleteDialog.open()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (sidebar.searching) {
                sidebar.endSearch();
            } else if (window.mode === "play") {
                window.pick("");
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            tall: true
            frame: window.frame
            showTitle: false

            // The title bar puts its content 16px in; the board's icon is 14.
            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: "qrc:/qt/qml/WildOS/Voice/data/wildos-voice.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Voice")
                    font.weight: UI.Style.weightMedium
                    color: window.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                selectedPath: window.mode === "record" ? Recorder.path : window.selectedPath
                onPicked: path => window.pick(path === Recorder.path ? "" : path)
                onRowMenu: (recording, row, x, y) => window.recordingMenu(recording, row, x, y)
            }

            MainPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                mode: window.mode
                recording: window.selected
                bars: window.bars
                onRenameRequested: renameDialog.ask()
                onDeleteRequested: deleteDialog.open()
                onNewRecordingRequested: {
                    Player.pause();
                    window.pick("");
                }
            }
        }
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }

    UI.Menu {
        id: menu
    }

    UI.Dialog {
        id: renameDialog
        function ask() {
            nameField.text = window.selected.title ?? "";
            nameField.error = false;
            open();
            nameField.forceActiveFocus();
            nameField.selectAll();
        }
        function confirm() {
            if (!Recordings.rename(window.selected.path, nameField.text)) {
                nameField.error = true;
                return;
            }
            close();
        }
        title: qsTr("Rename recording")
        text: qsTr("Its markers and transcript keep the same name, so they stay with it.")

        UI.TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            onAccepted: renameDialog.confirm()
        }

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: renameDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Rename")
                onClicked: renameDialog.confirm()
            }
        ]
    }

    UI.Dialog {
        id: deleteDialog
        title: qsTr("Delete %1?").arg(window.selected.title ?? "")
        text: qsTr("The recording, its markers and its transcript go to the bin.")

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: deleteDialog.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Delete")
                onClicked: {
                    const path = window.selected.path;
                    const title = window.selected.title;
                    deleteDialog.close();
                    Player.load("");
                    if (Recordings.remove(path)) {
                        window.say(qsTr("%1 moved to the bin").arg(title), qsTr("Undo"), () => {
                            if (Recordings.restore(path)) {
                                window.pick(path);
                            }
                        });
                    }
                }
            }
        ]
    }
}
