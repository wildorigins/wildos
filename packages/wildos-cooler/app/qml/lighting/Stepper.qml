import QtQuick
import QtQuick.Templates as T
import WildOS.Cooler
import WildOS.UI as UI

// A whole number with a minus and a plus (10.15 "Lights plugged in"): 30px
// tall, the number in the middle. Holding a button keeps counting; the
// number can also be typed, and Up and Down step it.
Rectangle {
    id: stepper

    property int value: 0
    property int from: 0
    property int to: 100
    property string accessibleName

    // The number on show runs ahead of `value` while a button is held or
    // the answer is on its way: `moved` is sent once it has stopped.
    property int pending: -1
    readonly property int shown: pending >= 0 ? pending : value

    signal moved(int value)

    function set(wanted) {
        const next = Math.max(from, Math.min(to, wanted));
        if (next !== shown) {
            pending = next;
            settle.restart();
            giveUp.restart();
        }
    }

    onValueChanged: if (value === pending) pending = -1

    Timer {
        id: settle
        interval: 300
        onTriggered: if (stepper.pending >= 0 && stepper.pending !== stepper.value) stepper.moved(stepper.pending)
    }

    Timer {
        id: giveUp
        interval: 2500
        onTriggered: stepper.pending = -1
    }

    implicitWidth: 1 + 26 + Math.max(28, field.contentWidth + 8) + 26 + 1
    implicitHeight: 30
    radius: 7
    color: UI.Style.tone(0.04)
    border.width: 1
    border.color: field.activeFocus ? UI.Style.accent : UI.Style.borderSubtle

    // An inline component cannot see this file's ids, so each button is
    // told what it needs.
    component Step: T.AbstractButton {
        id: step

        property string iconName

        width: 26
        y: 1
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        autoRepeat: true
        autoRepeatDelay: 350
        autoRepeatInterval: 60
        opacity: enabled ? 1 : 0.4
        Accessible.role: Accessible.Button

        background: Rectangle {
            radius: 6
            color: step.down ? UI.Style.surfacePressed : step.hovered ? UI.Style.surfaceHover : "transparent"
        }

        contentItem: Item {
            LightIcon {
                anchors.centerIn: parent
                name: step.iconName
                size: 13
                strokeWidth: 2
            }
        }
    }

    Step {
        x: 1
        height: stepper.height - 2
        iconName: "minus"
        enabled: stepper.shown > stepper.from
        Accessible.name: qsTr("Fewer")
        onClicked: stepper.set(stepper.shown - 1)
    }

    TextInput {
        id: field
        x: 27
        width: parent.width - 54
        height: parent.height
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        text: stepper.shown
        color: UI.Style.ink
        selectionColor: UI.Style.accent
        selectedTextColor: UI.Style.textOnAccent
        font.family: UI.Style.fontFamily
        font.pixelSize: 13 * UI.Style.textScale
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: 0; top: 9999 }
        selectByMouse: true
        activeFocusOnTab: true
        Accessible.role: Accessible.SpinBox
        Accessible.name: stepper.accessibleName
        onEditingFinished: {
            stepper.set(parseInt(text) || 0);
            text = Qt.binding(() => stepper.shown);
        }
        Keys.onUpPressed: stepper.set(stepper.shown + 1)
        Keys.onDownPressed: stepper.set(stepper.shown - 1)
        Keys.onEscapePressed: {
            text = Qt.binding(() => stepper.shown);
            focus = false;
        }
    }

    Step {
        x: parent.width - 27
        height: stepper.height - 2
        iconName: "plus"
        enabled: stepper.shown < stepper.to
        Accessible.name: qsTr("More")
        onClicked: stepper.set(stepper.shown + 1)
    }
}
