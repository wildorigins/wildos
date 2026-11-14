import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Voice
import WildOS.UI as UI

// The list of recordings (AppVoice.dc): "Recordings" with search, the
// recordings grouped Today, This week and Earlier, and the "Stays on this
// PC" card at the foot.
Rectangle {
    id: side

    property string selectedPath
    property string query
    readonly property bool searching: searchField.visible

    signal picked(string path)
    signal rowMenu(var recording, Item row, real x, real y)

    color: VoiceStyle.sideShade

    function startSearch() {
        searchField.visible = true;
        searchField.forceActiveFocus();
    }
    function endSearch() {
        searchField.text = "";
        searchField.visible = false;
    }

    // The list, with the recording in progress at the top even before the
    // folder scan has found its file.
    readonly property var shown: {
        const needle = query.trim().toLowerCase();
        const list = [];
        let live = false;
        for (const item of Recordings.items) {
            if (item.path === Recorder.path) {
                live = true;
            }
            if (needle.length > 0 && !item.title.toLowerCase().includes(needle) && !(item.transcript ?? "").toLowerCase().includes(needle)) {
                continue;
            }
            list.push(Object.assign({ group: VoiceStyle.group(item.created) }, item));
        }
        if (Recorder.active && !live && needle.length === 0) {
            list.unshift({ path: Recorder.path, title: Recorder.title, created: new Date(), duration: 0, group: VoiceStyle.group(new Date()) });
        }
        // Each group's name goes above its first recording.
        for (let i = 0; i < list.length; ++i) {
            list[i].heading = i === 0 || list[i].group !== list[i - 1].group ? list[i].group : "";
        }
        return list;
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: UI.Style.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 54

            UI.Label {
                visible: !searchField.visible
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 4
                text: qsTr("Recordings")
                size: 16
                font.weight: UI.Style.weightBold
            }

            UI.SearchField {
                id: searchField
                visible: false
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: searchButton.left
                anchors.rightMargin: 6
                anchors.verticalCenter: searchButton.verticalCenter
                placeholderText: qsTr("Search recordings")
                onTextChanged: side.query = text
                onEscapePressed: side.endSearch()
                // An empty box closes when you leave it, but not for the text
                // menu, which holds focus while it is up and may be there to
                // paste.
                onActiveFocusChanged: {
                    if (!activeFocus && !textMenuOpen && text.length === 0) {
                        visible = false;
                    }
                }
                onTextMenuFocusLeft: {
                    if (text.length === 0) {
                        visible = false;
                    }
                }
            }

            ToolButton {
                id: searchButton
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 4
                iconName: searchField.visible ? "close" : "search"
                tip: searchField.visible ? qsTr("Close search") : qsTr("Search")
                onClicked: searchField.visible ? side.endSearch() : side.startSearch()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: side.shown
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: 8
            delegate: Column {
                id: entry
                required property var modelData
                width: ListView.view.width

                UI.Label {
                    visible: entry.modelData.heading.length > 0
                    width: parent.width
                    height: 32
                    leftPadding: 16
                    topPadding: 12
                    bottomPadding: 4
                    verticalAlignment: Text.AlignBottom
                    text: entry.modelData.heading
                    size: 12
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkSubtle
                }

                RecordingRow {
                    id: rowItem
                    x: 8
                    width: parent.width - 16
                    recording: entry.modelData
                    selected: entry.modelData.path === side.selectedPath
                    recordingNow: Recorder.active && entry.modelData.path === Recorder.path
                    liveLength: Recorder.elapsed
                    onClicked: side.picked(entry.modelData.path)
                    onMenuRequested: (x, y) => side.rowMenu(entry.modelData, rowItem, x, y)
                }
            }

            T.ScrollBar.vertical: UI.ScrollBar {}

            UI.Label {
                visible: list.count === 0 && Recordings.loaded
                anchors.horizontalCenter: parent.horizontalCenter
                y: 40
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: side.query.length > 0 ? qsTr("No recordings match.") : qsTr("Recordings you make appear here.")
                size: 13
                color: UI.Style.inkMuted
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            implicitHeight: footRow.implicitHeight + 22
            radius: 10
            color: UI.Style.surfaceRaised
            border.width: 1
            border.color: UI.Style.borderSubtle

            Row {
                id: footRow
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                VoiceIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "shield-check"
                    size: 18
                    strokeWidth: 1.8
                    color: VoiceStyle.safe
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    UI.Label {
                        text: qsTr("Stays on this PC")
                        size: 12
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Label {
                        text: qsTr("Recordings and transcripts")
                        size: 11
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }
}
