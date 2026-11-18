import QtQuick
import QtQuick.Templates as T

// The design's range input, as in the quick settings flyout (Flyouts.dc):
// an 8px track with a hairline edge, filled with the accent up to a 16px
// accent thumb that grows a little under the pointer. From wildos-settings
// WSlider, which the flyout's LevelSlider copies; Page Up and Down move by
// ten as on Windows.
//
// The mouse wheel leaves the value alone, so scrolling a page never drags a
// slider that passes under the pointer. A slider in a flyout that does not
// scroll (volume, brightness) sets `wheelEnabled: true`.
T.Slider {
    id: control

    property string accessibleName
    // Set while the value is shown muted (sound off), so the fill reads as
    // inactive.
    property bool dimmed: false
    // The flyout look from EmptyStates.dc: a 4px track (tone 0.16, radius
    // 2) filled with the accent, and a 16px white knob.
    property bool flat: false
    property bool previewHover: false
    property bool previewFocus: false

    readonly property bool lit: (hovered || previewHover) && enabled

    implicitHeight: 36
    implicitWidth: 240
    from: 0
    to: 100
    stepSize: 1
    wheelEnabled: false
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : Style.disabledOpacity
    Accessible.name: accessibleName

    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
            const step = (to - from) / 10;
            value = Math.max(from, Math.min(to, value + (event.key === Qt.Key_PageUp ? step : -step)));
            moved();
            event.accepted = true;
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.flat ? 4 : 8

        Rectangle {
            anchors.fill: parent
            radius: control.flat ? 2 : 4
            color: control.flat ? Style.borderControl : "transparent"
            border.width: control.flat ? 0 : 1
            border.color: Style.surfaceTrack
        }

        Rectangle {
            x: control.flat ? 0 : 1
            y: control.flat ? 0 : 1
            width: Math.max(0, control.visualPosition * parent.width - (control.flat ? 0 : 1))
            height: parent.height - (control.flat ? 0 : 2)
            radius: control.flat ? 2 : 3
            color: control.dimmed ? Style.inkSubtle : Style.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        color: control.flat ? Style.sliderKnob : control.dimmed ? Style.inkSubtle : Style.accent
        border.width: control.flat && !Style.dark ? 1 : 0
        border.color: Style.borderControl
        scale: control.pressed ? 1.12 : control.lit ? 1.06 : 1
        Behavior on scale {
            NumberAnimation { duration: Style.motionFast }
        }

        FocusRing {
            visible: control.visualFocus || control.previewFocus
            targetRadius: 8
        }
    }
}
