import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A button on PDF's toolbar (AppPdf.dc): an icon, and a word for the tools
// (32px high, 10px in, radius 6, 13px text 7px after a 16px icon), or an
// icon alone (34 by 34, radius 8). The tool in use has the neutral selected
// fill with its icon in the accent.
T.Button {
    id: control

    property string iconName
    property real iconSize: 16
    property bool active: false
    property string tip

    readonly property bool iconOnly: text.length === 0
    readonly property bool lit: hovered && enabled

    implicitWidth: iconOnly ? 34 : row.implicitWidth + 20
    implicitHeight: iconOnly ? 34 : 32
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    Accessible.name: iconOnly ? tip : text
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: control.iconOnly ? UI.Style.radiusSm : UI.Style.radiusXs
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.lit
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: control.down ? UI.Style.surfacePressed : control.active ? UI.Style.surfaceSelected : control.lit ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7

            PdfIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: control.iconSize
                color: control.active ? UI.Style.accent : UI.Style.iconInk
            }

            UI.Label {
                visible: !control.iconOnly
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                size: 13
            }
        }
    }

    UI.Tooltip {
        text: control.tip
        visible: control.hovered && control.tip.length > 0 && !control.down
    }
}
