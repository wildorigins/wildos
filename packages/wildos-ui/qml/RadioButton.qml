import QtQuick
import QtQuick.Templates as T

// The design's radio (Settings.dc protection cards): a 16px ring, 1.5px in
// radio-ring ink when off; on, a 5px accent ring round an on-accent centre.
// From wildos-settings RadioCard. The radio sits left of its label, as on
// Windows (TaskbarPage drew it on the right with the toggle's ring colour).
T.RadioButton {
    id: control

    property int size: 13
    property bool previewHover: false
    property bool previewFocus: false

    readonly property bool lit: (hovered || previewHover) && enabled

    implicitWidth: indicator.width + (text.length > 0 ? spacing + label.implicitWidth : 0)
    implicitHeight: Math.max(28, label.implicitHeight)
    spacing: 10
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : Style.disabledOpacity
    Accessible.name: text

    indicator: Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 8
        color: control.checked ? Style.accentDot : control.lit ? Style.surfaceHover : "transparent"
        border.width: control.checked ? (control.down ? 6 : 5) : 1.5
        border.color: control.checked ? (control.lit ? Style.accentHover : Style.accent) : Style.radioRing
        Behavior on border.width {
            NumberAnimation { duration: Style.motionFast }
        }

        FocusRing {
            visible: control.visualFocus || control.previewFocus
            targetRadius: 8
        }
    }

    contentItem: Label {
        id: label
        visible: control.text.length > 0
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        size: control.size
    }
}
