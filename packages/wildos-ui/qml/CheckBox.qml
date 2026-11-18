import QtQuick
import QtQuick.Templates as T

// The design's check box (BringFiles.dc, StarterApps.dc): a 20px square
// with a 6px radius, outlined at 1.5px when clear, accent filled with a
// 13px on-accent tick (stroke 3) when ticked. From wildos-welcome
// CheckMark, the only implementation that matches; wildos-files CheckRow
// draws 18px at radius 5. A partly ticked box shows a dash.
T.CheckBox {
    id: control

    property int size: 13
    property bool previewHover: false
    property bool previewFocus: false

    readonly property bool lit: (hovered || previewHover) && enabled
    readonly property bool marked: checkState !== Qt.Unchecked

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
        width: 20
        height: 20
        radius: 6
        color: control.marked ? (control.lit ? Style.accentHover : Style.accent) : control.lit ? Style.surfaceHover : "transparent"
        border.width: control.marked ? 0 : 1.5
        border.color: Style.toggleOffBorder
        scale: control.down ? 0.92 : 1
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }
        Behavior on scale {
            NumberAnimation { duration: Style.motionFast }
        }

        Icon {
            visible: control.checkState === Qt.Checked
            anchors.centerIn: parent
            name: "checkmark"
            size: 13
            strokeWidth: 3
            color: Style.textOnAccent
        }

        Rectangle {
            visible: control.checkState === Qt.PartiallyChecked
            anchors.centerIn: parent
            width: 10
            height: 2.5
            radius: 1.25
            color: Style.textOnAccent
        }

        FocusRing {
            visible: control.visualFocus || control.previewFocus
            targetRadius: 6
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
