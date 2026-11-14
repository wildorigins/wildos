import QtQuick
import QtQuick.Effects
import WildOS.UI as UI

// The Capture toolbar at the top of the main screen (AppCapture.dc): 6px
// in, radius 12, on the panel glass with a 16px 40px shadow. Screenshot,
// Record or Scan (which opens the Scan window); area, window, full screen or freeform; the delay; options; the
// accent Capture button and Close. Nothing in it takes the keyboard, so
// Space, Enter and Escape always reach the overlay.
Item {
    id: bar

    // Given the keyboard back after a menu closes.
    property Item keyTarget

    implicitWidth: row.implicitWidth + 14
    implicitHeight: 46

    readonly property var delays: [0, 3, 5, 10]
    function delayName(seconds: int): string {
        return seconds === 0 ? qsTr("No delay") : qsTr("%1 seconds").arg(seconds);
    }
    readonly property bool canCapture: CaptureApp.mode === "screen"
        || (CaptureApp.mode === "rectangle" && CaptureApp.pickedScreen.length > 0)
        || (CaptureApp.mode === "window" && CaptureApp.pickedWindow.id !== undefined)

    component Divider: Rectangle {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        width: 13
        height: 24
        color: "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 24
            color: UI.Style.divider
        }
    }

    component Mode: ToolButton {
        required property string mode
        tool: true
        focusPolicy: Qt.NoFocus
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        active: CaptureApp.mode === mode
        onClicked: CaptureApp.mode = mode
    }

    Rectangle {
        id: glass
        anchors.fill: parent
        radius: UI.Style.radiusMd
        color: UI.Style.glassPanel
        border.width: 1
        border.color: UI.Style.borderContextMenu
        visible: false
    }

    MultiEffect {
        anchors.fill: glass
        source: glass
        shadowEnabled: true
        shadowColor: UI.Style.shadowMenu
        shadowBlur: 1
        shadowVerticalOffset: 16
        autoPaddingEnabled: true
    }

    // Clicks on the bar between its buttons stay on the bar instead of
    // starting a selection underneath.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: kinds.implicitWidth + 6
            height: 32
            radius: 7
            color: UI.Style.surfaceRest
            border.width: 1
            border.color: UI.Style.borderField

            Row {
                id: kinds
                anchors.centerIn: parent

                UI.SegmentButton {
                    height: 26
                    focusPolicy: Qt.NoFocus
                    checkable: false
                    text: qsTr("Screenshot")
                    checked: !CaptureApp.recordMode
                    onClicked: CaptureApp.recordMode = false
                }
                UI.SegmentButton {
                    height: 26
                    focusPolicy: Qt.NoFocus
                    checkable: false
                    text: qsTr("Record")
                    enabled: CaptureApp.canRecord
                    checked: CaptureApp.recordMode
                    onClicked: CaptureApp.recordMode = true
                }
                // Paper rather than the screen: opens the Scan window.
                UI.SegmentButton {
                    height: 26
                    focusPolicy: Qt.NoFocus
                    checkable: false
                    text: qsTr("Scan")
                    checked: false
                    onClicked: {
                        CaptureApp.cancel();
                        CaptureApp.openScan();
                    }
                }
            }
        }

        Divider {}

        Mode {
            mode: "rectangle"
            iconName: "area"
            tip: qsTr("Area")
        }
        Mode {
            mode: "window"
            iconName: "window"
            tip: qsTr("Window")
        }
        Mode {
            mode: "screen"
            iconName: "screen"
            tip: qsTr("Full screen")
        }
        Mode {
            mode: "freehand"
            iconName: "freeform"
            tip: CaptureApp.recordMode ? qsTr("A freeform shape can't be recorded") : qsTr("Freeform")
            enabled: !CaptureApp.recordMode
        }

        Divider {}

        ToolButton {
            id: delayButton
            anchors.verticalCenter: parent.verticalCenter
            focusPolicy: Qt.NoFocus
            iconName: "timer"
            dropDown: true
            text: bar.delayName(CaptureApp.delay)
            active: delayMenu.visible
            onClicked: delayMenu.popup(delayButton, 0, delayButton.height + 6)

            UI.Menu {
                id: delayMenu
                minimumWidth: 160
                entries: bar.delays.map(seconds => ({
                    text: bar.delayName(seconds),
                    radio: true,
                    checked: CaptureApp.delay === seconds,
                    run: () => CaptureApp.delay = seconds
                }))
                onClosed: if (bar.keyTarget) bar.keyTarget.forceActiveFocus()
            }
        }

        ToolButton {
            id: optionsButton
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
            focusPolicy: Qt.NoFocus
            iconName: "options"
            tip: qsTr("Options")
            active: optionsMenu.visible
            onClicked: optionsMenu.popup(optionsButton, 0, optionsButton.height + 6)

            UI.Menu {
                id: optionsMenu
                minimumWidth: 280
                entries: [
                    { text: qsTr("Open the editor after a screenshot"), checked: CaptureApp.openEditor, run: () => CaptureApp.openEditor = !CaptureApp.openEditor },
                    { text: qsTr("Save screenshots to Pictures › Screenshots"), checked: CaptureApp.autoSave, run: () => CaptureApp.autoSave = !CaptureApp.autoSave },
                    { text: qsTr("Show the pointer"), checked: CaptureApp.includePointer, run: () => CaptureApp.includePointer = !CaptureApp.includePointer },
                    { separator: true },
                    // The overlay covers the whole screen and would hide Report a problem.
                    { text: qsTr("Report a problem"), run: () => {
                        Qt.openUrlExternally("wildos-report:capture");
                        CaptureApp.cancel();
                    } }
                ]
                onClosed: if (bar.keyTarget) bar.keyTarget.forceActiveFocus()
            }
        }

        Divider {}

        ToolButton {
            anchors.verticalCenter: parent.verticalCenter
            focusPolicy: Qt.NoFocus
            primary: true
            iconName: CaptureApp.recordMode ? "record" : "camera"
            text: CaptureApp.recordMode ? qsTr("Record") : qsTr("Capture")
            tip: CaptureApp.mode === "freehand" ? qsTr("Draw round what you want to capture it")
                : CaptureApp.mode === "window" && !bar.canCapture ? qsTr("Point at a window first")
                : CaptureApp.mode === "rectangle" && !bar.canCapture ? qsTr("Drag to pick an area first") : ""
            enabled: bar.canCapture
            onClicked: CaptureApp.capture(CaptureApp.primaryScreen)
        }

        ToolButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
            focusPolicy: Qt.NoFocus
            iconName: "x"
            tip: qsTr("Close (Esc)")
            onClicked: CaptureApp.cancel()
        }
    }
}
