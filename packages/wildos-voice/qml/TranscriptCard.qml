import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Voice
import WildOS.UI as UI

// The Transcript card (AppVoice.dc): "Transcript", the "Made on this PC"
// switch and the lines so far, each after the time it was said. While
// recording, the newest line is in full ink with the accent caret after it.
Rectangle {
    id: card

    // "idle", "record" or "play".
    property string mode: "idle"
    property string path
    property bool canSeek: false

    signal seek(double ms)

    readonly property bool working: Transcripts.workingPath.length > 0 && Transcripts.workingPath === path
    readonly property bool hasLines: Transcripts.lines.length > 0

    radius: UI.Style.radiusPanel
    color: UI.Style.surfaceRaised
    border.width: 1
    border.color: UI.Style.borderSubtle
    implicitHeight: column.implicitHeight + 28

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 14
        anchors.bottomMargin: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Transcript")
                size: 13.5
                font.weight: UI.Style.weightSemiBold
            }
            UI.Label {
                text: qsTr("Made on this PC")
                size: 12
                color: UI.Style.inkMuted
            }
            UI.Toggle {
                checked: Transcripts.enabled || Transcripts.downloading
                enabled: Transcripts.supported
                Accessible.name: qsTr("Transcripts, made on this PC")
                onToggled: Transcripts.enabled = checked
            }
        }

        // The speech model coming down, once.
        ColumnLayout {
            visible: Transcripts.downloading
            Layout.fillWidth: true
            spacing: 10

            UI.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Getting the speech model (142 MB). It's downloaded once; after that, transcripts are made on this PC and nothing you say leaves it.")
                size: 14
                color: UI.Style.inkMuted
                lineHeight: 1.4
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                UI.ProgressBar {
                    Layout.fillWidth: true
                    value: Transcripts.downloadProgress
                }
                UI.Label {
                    text: qsTr("%1%").arg(Math.floor(Transcripts.downloadProgress * 100))
                    size: 12
                    color: UI.Style.inkMuted
                    font.features: { "tnum": 1 }
                }
                UI.Button {
                    text: qsTr("Cancel")
                    size: "small"
                    onClicked: Transcripts.cancelDownload()
                }
            }
        }

        UI.Label {
            id: note
            visible: !Transcripts.downloading && text.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            size: 14
            lineHeight: 1.4
            color: Transcripts.message.length > 0 ? UI.Style.warning : UI.Style.inkMuted
            text: {
                if (Transcripts.message.length > 0) {
                    return Transcripts.message;
                }
                if (!Transcripts.supported) {
                    return qsTr("This PC's processor is too old for the speech model, so it can't make transcripts.");
                }
                if (card.hasLines) {
                    return "";
                }
                if (!Transcripts.enabled) {
                    return card.mode === "play"
                        ? qsTr("Turn this on to write down what's said in your recordings. It's done on this PC with a speech model that's downloaded once (142 MB). Nothing you say leaves this PC.")
                        : qsTr("Turn this on to write down what's said as you record. It's done on this PC with a speech model that's downloaded once (142 MB). Nothing you say leaves this PC.");
                }
                if (card.mode === "record") {
                    return qsTr("Lines appear here every few seconds as they're written down.");
                }
                if (card.mode === "idle") {
                    return qsTr("What's said is written down as you record.");
                }
                if (card.working) {
                    return Transcripts.fileProgress >= 0 ? qsTr("Writing it down: %1%").arg(Math.floor(Transcripts.fileProgress * 100)) : qsTr("Writing down the last few seconds.");
                }
                return qsTr("This recording has no transcript yet.");
            }
        }

        RowLayout {
            visible: card.mode === "play" && Transcripts.enabled && Transcripts.modelReady && !card.hasLines && !card.working && Transcripts.message.length === 0
            spacing: 10

            UI.Button {
                text: qsTr("Make a transcript")
                enabled: Transcripts.workingPath.length === 0
                onClicked: Transcripts.transcribe(card.path)
            }
            UI.Label {
                visible: Transcripts.workingPath.length > 0
                text: qsTr("Another transcript is being made.")
                size: 12
                color: UI.Style.inkMuted
            }
        }

        ListView {
            id: lines
            visible: card.hasLines
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: contentHeight
            Layout.minimumHeight: Math.min(contentHeight, 46)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Transcripts.lines
            // New lines while recording keep the end in view.
            onCountChanged: {
                if (card.mode === "record") {
                    Qt.callLater(positionViewAtEnd);
                }
            }

            delegate: Item {
                id: lineItem
                required property var modelData
                required property int index
                readonly property bool newest: card.mode === "record" && index === lines.count - 1
                width: ListView.view.width
                height: text.implicitHeight

                UI.Label {
                    id: text
                    width: parent.width
                    wrapMode: Text.Wrap
                    maximumLineCount: 0
                    textFormat: Text.StyledText
                    size: 14
                    lineHeight: 1.65
                    color: lineItem.newest ? UI.Style.ink : UI.Style.inkMuted
                    text: (lineItem.modelData.time >= 0 ? "<font color=\"" + UI.Style.inkSubtle + "\">" + VoiceStyle.time(lineItem.modelData.time) + "</font> " : "")
                        + lineItem.modelData.text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                        // The board's accent caret: more is still being written.
                        + (lineItem.newest && card.working ? "<font color=\"" + UI.Style.accent + "\">│</font>" : "")
                }

                MouseArea {
                    visible: card.canSeek && lineItem.modelData.time >= 0
                    width: 48
                    height: 22
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.seek(lineItem.modelData.time)
                }
            }

            T.ScrollBar.vertical: UI.ScrollBar {}
        }
    }
}
