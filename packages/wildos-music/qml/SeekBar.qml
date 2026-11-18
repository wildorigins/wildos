import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// The player bar's seek and volume bars (AppMusic.dc): a 4px track with a
// fill in the scheme's ink and, for seeking, a 12px knob of the same. The bar follows `source`
// except while it is being dragged; `committed` reports where it was let go.
T.Slider {
    id: control

    property real source: 0
    property bool knob: true
    property color fill: MusicStyle.barFill
    property string accessibleName
    // Shows the time under the pointer in a small label above the bar.
    property bool timeTip: false

    signal committed(real value)

    implicitHeight: 16
    implicitWidth: 420
    padding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    wheelEnabled: false
    Accessible.name: accessibleName
    Accessible.role: Accessible.Slider

    Binding on value {
        value: control.source
        when: !control.pressed
        restoreMode: Binding.RestoreNone
    }
    onPressedChanged: {
        if (!pressed) {
            committed(value);
        }
    }
    Keys.onLeftPressed: committed(Math.max(from, value - (to - from) / 20))
    Keys.onRightPressed: committed(Math.min(to, value + (to - from) / 20))

    HoverHandler {
        id: pointer
    }

    Rectangle {
        id: tip
        readonly property real at: control.pressed ? control.visualPosition * control.availableWidth
                                                   : Math.max(0, Math.min(control.availableWidth, pointer.point.position.x))
        visible: control.timeTip && control.enabled && (pointer.hovered || control.pressed)
        x: Math.round(Math.max(0, Math.min(control.width - width, at - width / 2)))
        y: -height - 8
        z: 1
        width: tipLabel.implicitWidth + 16
        height: tipLabel.implicitHeight + 8
        radius: UI.Style.radiusXs
        color: UI.Style.glassMenu
        border.width: 1
        border.color: UI.Style.borderMenu

        UI.Label {
            id: tipLabel
            anchors.centerIn: parent
            size: 12
            text: MusicStyle.time(control.pressed ? control.value : control.from + tip.at / Math.max(1, control.availableWidth) * (control.to - control.from))
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4

        Rectangle {
            anchors.fill: parent
            radius: 2
            color: MusicStyle.barTrack
        }
        Rectangle {
            width: Math.max(0, Math.min(parent.width, control.visualPosition * parent.width))
            height: parent.height
            radius: 2
            color: control.enabled ? control.fill : MusicStyle.barTrack
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * control.availableWidth - width / 2
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 12
        height: 12
        radius: 6
        visible: control.enabled && (control.knob || control.hovered || control.pressed || control.visualFocus)
        color: MusicStyle.barFill
        scale: control.pressed ? 1.15 : 1
        Behavior on scale {
            NumberAnimation { duration: UI.Style.motionFast }
        }

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: 6
        }
    }
}
