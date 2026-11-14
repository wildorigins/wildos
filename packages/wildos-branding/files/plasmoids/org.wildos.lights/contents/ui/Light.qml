import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The taskbar light: a 36 px tall pill, 10 px padding, its device's colour
// at 20% behind a 17 px icon and a 7 px dot, amber for the microphone and
// green for the camera; both pills when both are in use. A device switched
// off for every app, and not in use, shows its struck-through icon in a
// plain 32 px cell like the shield's, so the switch can be found again.
MouseArea {
    id: light

    required property var lights

    property bool wasExpanded: false

    readonly property var pills: [
        { name: "microphone", on: lights.micInUse, off: lights.microphone.blocked, color: lights.amber, path: Glyphs.mic, offPath: Glyphs.micOff },
        { name: "camera", on: lights.cameraInUse, off: lights.camera.blocked, color: lights.green, path: Glyphs.camera, offPath: Glyphs.cameraOff }
    ].filter(p => p.on || p.off)

    Layout.minimumWidth: lights.shown ? row.implicitWidth : 0
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: 44

    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: lights.summary
    Accessible.description: lights.toolTipSubText

    // Remembering the state at press time stops a click that closes the popup
    // (it loses focus first) from opening it again straight away.
    onPressed: wasExpanded = lights.expanded
    onClicked: lights.expanded = !wasExpanded

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        visible: light.lights.shown

        Repeater {
            model: light.pills

            Rectangle {
                id: pill
                required property var modelData
                readonly property bool lit: modelData.on
                width: lit ? 10 + 17 + 7 + 7 + 10 : 32
                height: 36
                radius: lit ? 10 : 8
                color: {
                    const c = modelData.color;
                    const base = lit ? Qt.rgba(c.r, c.g, c.b, 0.20) : "transparent";
                    if (light.pressed) {
                        return lit ? Qt.rgba(c.r, c.g, c.b, 0.34) : UI.Style.surfacePressed;
                    }
                    if (light.lights.expanded || light.containsMouse) {
                        return lit ? Qt.rgba(c.r, c.g, c.b, 0.28) : light.lights.expanded ? UI.Style.surfaceSelected : UI.Style.surfaceHover;
                    }
                    return base;
                }
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                UI.Glyph {
                    visible: pill.lit
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 17
                    height: 17
                    path: pill.modelData.path
                    strokeWidth: 2.2
                    color: pill.modelData.color
                }

                Rectangle {
                    visible: pill.lit
                    x: 10 + 17 + 7
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 3.5
                    color: pill.modelData.color
                }

                UI.Glyph {
                    visible: !pill.lit
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    path: pill.modelData.offPath
                    strokeWidth: 2
                    color: UI.Style.iconInk
                }
            }
        }
    }
}
