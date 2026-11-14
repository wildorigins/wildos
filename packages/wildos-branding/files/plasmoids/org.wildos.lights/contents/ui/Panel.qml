pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import WildOS.UI as UI

// The panel from TaskbarCamMic.dc: 380 wide with 18 px inside and 14 px
// between rows. What is in use now, each app with when it started and Stop;
// then each idle device with the app that used it last; a switch per
// device that blocks it for every app; and the way to this week's use in
// History. Plasma's theme draws the glass, so its frame margins come off
// the board's outer edge, as on the other flyouts.
FocusScope {
    id: panel

    required property var lights

    readonly property int panelWidth: 380
    readonly property int pad: 18

    KSvg.FrameSvgItem {
        id: frame
        imagePath: "dialogs/background"
        visible: false
    }

    readonly property real contentWidth: panelWidth - frame.margins.left - frame.margins.right
    readonly property real contentHeight: 2 * pad + body.implicitHeight - frame.margins.top - frame.margins.bottom

    Layout.minimumWidth: contentWidth
    Layout.preferredWidth: contentWidth
    Layout.maximumWidth: contentWidth
    Layout.minimumHeight: contentHeight
    Layout.preferredHeight: contentHeight
    Layout.maximumHeight: contentHeight

    focus: true

    // In use first, oldest first, then the devices nobody is using.
    readonly property var uses: lights.microphone.uses.map(u => Object.assign({ device: "microphone" }, u))
        .concat(lights.camera.uses.map(u => Object.assign({ device: "camera" }, u)))
        .sort((a, b) => a.since - b.since)
    readonly property var idle: [
        { name: "Microphone", info: lights.microphone, path: Glyphs.micOff, busy: lights.micInUse },
        { name: "Camera", info: lights.camera, path: Glyphs.cameraOff, busy: lights.cameraInUse }
    ].filter(d => !d.busy)

    Connections {
        target: panel.lights
        function onExpandedChanged() {
            if (panel.lights.expanded) {
                panel.forceActiveFocus();
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            lights.expanded = false;
            event.accepted = true;
        }
    }

    component Divider: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: UI.Style.tone(0.07)
    }

    component TwoLines: ColumnLayout {
        property alias title: titleLabel.text
        property alias detail: detailLabel.text
        Layout.fillWidth: true
        spacing: 2

        UI.Label {
            id: titleLabel
            Layout.fillWidth: true
            size: 13.5
            font.weight: 500
            elide: Text.ElideRight
        }

        UI.Label {
            id: detailLabel
            Layout.fillWidth: true
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    component Switch: RowLayout {
        id: switchRow
        required property string name
        required property string path
        required property var device
        spacing: 12
        Layout.fillWidth: true

        Item {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 19

            UI.Glyph {
                anchors.centerIn: parent
                width: 19
                height: 19
                path: switchRow.path
                strokeWidth: 1.8
                color: UI.Style.ink
            }
        }

        TwoLines {
            title: switchRow.name === "camera" ? "Camera" : "Microphone"
            detail: "Off here blocks it for every app."
        }

        UI.Toggle {
            // Read again whenever a request ends, so a cancelled password
            // prompt leaves the switch where the device really is.
            checked: !switchRow.device.blocked && panel.lights.requestCount >= 0
            locked: switchRow.device.maximum
            lockedReason: "Maximum protection keeps the camera off"
            Accessible.name: switchRow.name === "camera" ? "Camera" : "Microphone"
            onToggled: panel.lights.setAllowed(switchRow.name, checked)
        }
    }

    ColumnLayout {
        id: body
        x: panel.pad - frame.margins.left
        y: panel.pad - frame.margins.top
        width: panel.panelWidth - 2 * panel.pad
        spacing: 14

        UI.Label {
            visible: panel.uses.length > 0
            text: "IN USE NOW"
            size: 12.5
            font.weight: 600
            font.letterSpacing: 0.25
            color: UI.Style.inkSubtle
        }

        Repeater {
            model: panel.uses

            RowLayout {
                id: useRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 12

                Item {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30

                    Kirigami.Icon {
                        id: appIcon
                        anchors.fill: parent
                        source: useRow.modelData.icon || ""
                        visible: valid && source !== ""
                    }

                    UI.Glyph {
                        visible: !appIcon.visible
                        anchors.centerIn: parent
                        width: 19
                        height: 19
                        path: useRow.modelData.device === "camera" ? Glyphs.camera : Glyphs.mic
                        strokeWidth: 1.8
                        color: useRow.modelData.device === "camera" ? panel.lights.green : panel.lights.amber
                    }
                }

                TwoLines {
                    title: useRow.modelData.app + " is using the " + panel.lights.deviceWord(useRow.modelData.device)
                    detail: "Since " + panel.lights.timeText(useRow.modelData.since) + "."
                }

                UI.Button {
                    visible: useRow.modelData.stoppable === true
                    text: "Stop"
                    Accessible.name: "Stop " + useRow.modelData.app + " using the " + panel.lights.deviceWord(useRow.modelData.device)
                    onClicked: panel.lights.stopUse(useRow.modelData.key)
                }
            }
        }

        Repeater {
            model: panel.idle

            RowLayout {
                id: idleRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 12

                Item {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 19

                    UI.Glyph {
                        anchors.centerIn: parent
                        width: 19
                        height: 19
                        path: idleRow.modelData.path
                        strokeWidth: 1.8
                        color: UI.Style.inkMuted
                    }
                }

                TwoLines {
                    title: idleRow.modelData.name
                    detail: panel.lights.idleText(idleRow.modelData.info)
                }
            }
        }

        Divider {}

        Switch {
            name: "microphone"
            path: Glyphs.mic
            device: panel.lights.microphone
        }

        Switch {
            name: "camera"
            path: Glyphs.camera
            device: panel.lights.camera
        }

        Divider {}

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            UI.Label {
                Layout.fillWidth: true
                text: "Amber is the microphone. Green is the camera."
                size: 12.5
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            UI.LinkButton {
                // The link's own padding sits outside the panel's edge, so
                // its words line up with the switches above.
                Layout.rightMargin: -4
                text: "This week"
                Accessible.name: "This week's camera and microphone use in History"
                onClicked: panel.lights.openHistory()
            }
        }
    }
}
