import QtQuick
import QtQuick.Templates as T

// A thin accent bar for downloads, installs, copies and storage use. The
// App bridge bar (4px, radius 2, track 0.10, 200ms fill) is the base; the
// Store pill and Settings usage bar draw the same idea three other ways.
// `warning` turns the fill Sand, as the drive bars in Files do above 90%.
T.ProgressBar {
    id: control

    property real barHeight: 4
    property bool warning: false

    implicitWidth: 200
    implicitHeight: barHeight

    background: Rectangle {
        radius: control.barHeight / 2
        color: Style.tone(0.10)
    }

    contentItem: Item {
        clip: true

        Rectangle {
            visible: !control.indeterminate
            width: control.visualPosition * parent.width
            height: parent.height
            radius: control.barHeight / 2
            color: control.warning ? Style.warning : Style.accent
            Behavior on width {
                NumberAnimation { duration: Style.motionNormal; easing.type: Easing.OutCubic }
            }
        }

        Rectangle {
            id: sweep
            visible: control.indeterminate
            width: parent.width * 0.3
            height: parent.height
            radius: control.barHeight / 2
            color: Style.accent

            NumberAnimation on x {
                // Stops in hidden and minimised windows too (see Spinner).
                running: control.indeterminate && control.visible
                    && control.Window.visibility !== Window.Hidden
                    && control.Window.visibility !== Window.Minimized
                from: -sweep.width
                to: control.width
                duration: 1400
                loops: Animation.Infinite
                easing.type: Easing.InOutQuad
            }
        }
    }
}
