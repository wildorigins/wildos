import QtQuick
import QtQuick.Templates as T

// The design's switch (Personalisation.dc, Settings.dc): 42 by 24, an
// outlined pill with an ink knob when off, accent filled with an on-accent
// knob when on; the 13px knob stretches a little while held, as on
// Windows. From wildos-settings Toggle and wildos-welcome Toggle, whose
// `locked` state is kept: a locked switch stays hoverable so
// `lockedReason` can say why it cannot change, which a disabled control
// could not. Text, if set, sits to the left like the "On" / "Off" label in
// Windows Settings rows.
//
// A switch with no text, or whose text is only "On" or "Off", says what it
// turns on through `accessibleName`; on the right of a ListRow it takes the
// row's title by itself.
//
// Bind `checked` to the backend and write it back in onToggled; the binding
// survives the click.
T.Switch {
    id: control

    property string accessibleName
    property bool locked: false
    property string lockedReason
    property bool previewHover: false
    property bool previewFocus: false

    readonly property bool lit: (hovered || previewHover) && enabled && !locked

    implicitWidth: (text.length > 0 ? label.implicitWidth + spacing : 0) + 42
    implicitHeight: 24
    spacing: 12
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    checkable: !locked
    opacity: enabled && !locked ? 1 : Style.disabledOpacity
    Accessible.name: accessibleName.length > 0 ? accessibleName : text
    Accessible.description: locked ? lockedReason : ""

    indicator: Rectangle {
        id: track
        x: control.width - width
        anchors.verticalCenter: parent.verticalCenter
        width: 42
        height: 24
        radius: 12
        color: control.checked ? (control.lit ? Style.accentHover : Style.accent) : control.lit ? Style.surfaceHover : "transparent"
        border.width: control.checked ? 0 : 1.5
        border.color: Style.toggleOffBorder
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }

        Rectangle {
            readonly property real grow: control.down ? 3 : 0
            width: 13 + grow
            height: 13
            radius: 6.5
            y: 5.5
            x: control.checked ? track.width - width - 5 : 5.5
            color: control.checked ? Style.accentDot : Style.toggleOffKnob
            Behavior on x {
                NumberAnimation { duration: Style.motionFast; easing.type: Style.easeStandard }
            }
            Behavior on width {
                NumberAnimation { duration: Style.motionFast }
            }
        }

        FocusRing {
            visible: control.visualFocus || control.previewFocus
            targetRadius: 12
        }
    }

    contentItem: Label {
        id: label
        visible: control.text.length > 0
        text: control.text
        rightPadding: 54
        horizontalAlignment: Text.AlignRight
    }

    Tooltip {
        text: control.lockedReason
        visible: control.locked && control.hovered && control.lockedReason.length > 0
    }
}
