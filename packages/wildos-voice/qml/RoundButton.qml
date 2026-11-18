import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// The round buttons under the waveform (AppVoice.dc): 48px for Mark and
// Pause, 72px with a 6px halo for Stop, each with its name 14px below the
// row. `box` is the row's height, so the small ones sit level with the big
// one and every name lines up. `glyph` is "icon", "stop" (a white rounded
// square) or "record" (a white dot).
T.Button {
    id: control

    property real diameter: 48
    property real box: 72
    property string iconName
    property real iconSize: 19
    property string glyph: "icon"
    property color fill: VoiceStyle.roundButton
    property color fillHover: VoiceStyle.roundButtonHover
    property color halo: "transparent"
    property color iconColor: UI.Style.iconInk
    property string label

    implicitWidth: diameter
    implicitHeight: box + 14 + caption.implicitHeight
    padding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    Accessible.name: label
    Accessible.role: Accessible.Button

    background: Item {}

    contentItem: Item {
        Rectangle {
            id: disc
            y: (control.box - control.diameter) / 2
            width: control.diameter
            height: control.diameter
            radius: width / 2
            color: control.down ? Qt.darker(control.fillHover, 1.06) : control.hovered && control.enabled ? control.fillHover : control.fill
            Behavior on color {
                ColorAnimation { duration: UI.Style.hoverDuration }
            }

            Rectangle {
                visible: control.halo.a > 0
                anchors.centerIn: parent
                width: parent.width + 12
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 6
                border.color: control.halo
            }

            VoiceIcon {
                visible: control.glyph === "icon"
                anchors.centerIn: parent
                name: control.iconName
                size: control.iconSize
                color: control.iconColor
            }
            Rectangle {
                visible: control.glyph === "stop"
                anchors.centerIn: parent
                width: 24
                height: 24
                radius: 5
                color: VoiceStyle.recordGlyph
            }
            Rectangle {
                visible: control.glyph === "record"
                anchors.centerIn: parent
                width: 26
                height: 26
                radius: 13
                color: VoiceStyle.recordGlyph
            }

            UI.FocusRing {
                visible: control.visualFocus
                targetRadius: parent.radius
            }
        }

        UI.Label {
            id: caption
            anchors.horizontalCenter: disc.horizontalCenter
            y: control.box + 14
            text: control.label
            size: 12
            color: UI.Style.inkMuted
        }
    }
}
