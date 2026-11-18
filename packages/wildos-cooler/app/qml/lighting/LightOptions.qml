import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// Brightness and speed for an effect (10.11, 10.16), and for the whole PC
// whether the lights go off with the screen locked.
UI.Card {
    id: options

    property int brightness: 100
    property int speed: 35
    property string speedNote: qsTr("How fast the colours move")
    property bool lockRow: false
    property bool offWhenLocked: false

    signal brightnessMoved(int value)
    signal speedMoved(int value)
    signal lockToggled(bool off)

    radius: UI.Style.radiusPanel
    clipContent: false
    padding: 1

    contentItem: Column {
        OptionRow {
            iconName: "brightness"
            title: qsTr("Brightness")

            UI.Slider {
                id: bright
                width: 170
                flat: true
                from: 0
                to: 100
                onMoved: options.brightnessMoved(Math.round(value))

                // Left alone while it is being dragged: the answer to an
                // earlier position must not pull it back.
                Binding on value {
                    when: !bright.pressed
                    value: options.brightness
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        OptionRow {
            iconName: "gauge"
            title: qsTr("Speed")
            description: options.speedNote

            UI.Slider {
                id: fast
                width: 170
                flat: true
                from: 0
                to: 100
                onMoved: options.speedMoved(Math.round(value))

                Binding on value {
                    when: !fast.pressed
                    value: options.speed
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        OptionRow {
            visible: options.lockRow
            iconName: "moon"
            title: qsTr("Lights off when the screen is locked")
            description: qsTr("They come back when you sign in")

            UI.Toggle {
                checked: options.offWhenLocked
                onToggled: options.lockToggled(checked)
            }
        }
    }
}
