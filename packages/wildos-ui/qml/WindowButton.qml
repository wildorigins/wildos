import QtQuick
import QtQuick.Shapes

// Minimise, maximise, restore and close: 46 by 42 (52 in the Store's taller
// bar) with the thin 11px glyphs of the Wild OS window frame. Glyphs and
// strokes are wildos-settings WindowButton, which matches the artboards
// exactly (12 unit paths, 1.3 and 1.2 strokes, flat ends on minimise);
// dimming in a window that is not in front comes from wildos-files. Close
// turns the frame's red #c4463f (the Aurorae decoration's, so every window
// agrees) and keeps the window's top right corner when it is the last
// button.
Item {
    id: button

    property string kind: "close"
    property real cornerRadius: 0
    property bool windowActive: true
    property bool previewHover: false
    property bool previewPressed: false

    readonly property bool isClose: kind === "close"
    readonly property bool lit: mouse.containsMouse || previewHover
    readonly property bool down: mouse.pressed || previewPressed

    signal clicked()

    implicitWidth: Style.windowButtonWidth
    implicitHeight: Style.titleBarHeight
    Accessible.role: Accessible.Button
    Accessible.name: kind === "minimise" ? qsTr("Minimise") : kind === "maximise" ? qsTr("Maximise") : kind === "restore" ? qsTr("Restore") : qsTr("Close")
    Accessible.onPressAction: clicked()

    Rectangle {
        anchors.fill: parent
        topRightRadius: button.cornerRadius
        color: !button.lit && !button.down ? Qt.alpha(button.isClose ? Style.closeHover : Style.surfaceHover, 0)
             : button.isClose ? (button.down ? Style.closePressed : Style.closeHover)
             : button.down ? Style.surfacePressed : Style.surfaceHover
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    Shape {
        anchors.centerIn: parent
        width: 12
        height: 12
        scale: 11 / 12
        opacity: button.windowActive || button.lit ? 1 : 0.55
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: button.isClose && (button.lit || button.down) ? "#ffffff" : Style.iconInk
            strokeWidth: button.kind === "maximise" || button.kind === "restore" ? 1.2 : 1.3
            fillColor: "transparent"
            capStyle: button.isClose ? ShapePath.RoundCap : ShapePath.FlatCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: button.kind === "minimise" ? "M1.5 6h9"
                    : button.kind === "maximise" ? "M3.3 1.5h5.4a1.8 1.8 0 0 1 1.8 1.8v5.4a1.8 1.8 0 0 1-1.8 1.8H3.3a1.8 1.8 0 0 1-1.8-1.8V3.3a1.8 1.8 0 0 1 1.8-1.8z"
                    : button.kind === "restore" ? "M3.6 3.5h4.7a1.4 1.4 0 0 1 1.4 1.4v4.7a1.4 1.4 0 0 1-1.4 1.4H3.6a1.4 1.4 0 0 1-1.4-1.4V4.9a1.4 1.4 0 0 1 1.4-1.4zM4.4 1.5h4.9a1.8 1.8 0 0 1 1.8 1.8v4.9"
                    : "M2 2l8 8M10 2l-8 8"
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
    }

    Tooltip {
        text: button.Accessible.name
        visible: mouse.containsMouse && !mouse.pressed
    }
}
