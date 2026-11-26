import QtQuick
import QtQuick.Templates as T
import WildOS.Voice
import WildOS.UI as UI

// A recording in the list (AppVoice.dc): a round waveform icon (a red
// microphone while it records), the title, when it was made or "Recording
// now", and its length on the right.
T.ItemDelegate {
    id: row

    property var recording: ({})
    property bool selected: false
    property bool recordingNow: false
    property double liveLength: 0
    // The list has the keyboard and this is the row it is on.
    property bool keyboardFocus: false

    signal menuRequested(real x, real y)

    implicitHeight: 52
    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: recording.title ?? ""

    background: Rectangle {
        radius: UI.Style.radiusSm
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: row.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.selected ? UI.Style.surfaceSelected : row.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        // Inside the row: the list clips, and the rows touch.
        UI.FocusRing {
            anchors.margins: 0
            targetRadius: UI.Style.radiusSm - 4
            visible: row.keyboardFocus
        }
    }

    contentItem: Item {
        Rectangle {
            id: badge
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
            radius: 16
            color: row.recordingNow ? VoiceStyle.recordHalo : VoiceStyle.rowIcon

            VoiceIcon {
                anchors.centerIn: parent
                name: row.recordingNow ? "mic" : "waveform"
                size: 15
                color: row.recordingNow ? VoiceStyle.recordIcon : UI.Style.inkMuted
            }
        }

        Column {
            anchors.left: badge.right
            anchors.leftMargin: 12
            anchors.right: length.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            UI.Label {
                width: parent.width
                text: row.recording.title ?? ""
                size: 13.5
                font.weight: row.selected ? UI.Style.weightSemiBold : UI.Style.weightRegular
            }
            UI.Label {
                width: parent.width
                text: row.recordingNow ? (Recorder.state === "paused" ? qsTr("Paused") : qsTr("Recording now")) : row.recording.created ? VoiceStyle.when(row.recording.created) : ""
                size: 12
                color: row.recordingNow ? VoiceStyle.recordText : UI.Style.inkMuted
            }
        }

        UI.Label {
            id: length
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: VoiceStyle.time(row.recordingNow ? row.liveLength : row.recording.duration)
            size: 12
            color: UI.Style.inkMuted
            font.features: { "tnum": 1 }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => row.menuRequested(eventPoint.position.x, eventPoint.position.y)
    }
}
