import QtQuick
import QtQuick.Templates as T
import WildOS.Voice
import WildOS.UI as UI

// The microphone drop-down at the top right (AppVoice.dc): the mic, its
// name and a small chevron, opening a menu of the microphones there are.
// Fixed while recording, since a recording comes from one microphone.
T.Button {
    id: control

    readonly property bool canPick: Recorder.hasMicrophone && !Recorder.active

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 30
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    enabled: Recorder.hasMicrophone
    Accessible.name: qsTr("Microphone: %1").arg(Recorder.deviceName)

    background: Rectangle {
        radius: UI.Style.radiusXs
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: control.canPick && control.down ? UI.Style.surfacePressed : control.canPick && control.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8

            VoiceIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "mic"
                size: 15
                color: UI.Style.inkMuted
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: Recorder.hasMicrophone ? Recorder.deviceName : qsTr("No microphone found")
                size: 12.5
                color: UI.Style.inkMuted
            }
            VoiceIcon {
                visible: control.canPick
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                size: 10
                strokeWidth: 2.4
                color: UI.Style.inkSubtle
            }
        }
    }

    onClicked: {
        if (!canPick) {
            return;
        }
        const current = Recorder.deviceName;
        menu.entries = Recorder.devices.map(device => ({
            text: device.name,
            checked: device.name === current,
            radio: true,
            run: () => Recorder.deviceId = device.id
        }));
        menu.popup(control, control.width - menu.implicitWidth, control.height + 4);
    }

    UI.Menu {
        id: menu
    }
}
