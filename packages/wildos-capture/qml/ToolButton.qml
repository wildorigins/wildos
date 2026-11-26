import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A button on Capture's toolbars (AppCapture.dc, AppCaptureEdit.dc), after
// PDF's: a tool (36 by 32, radius 6, 17px icon), an icon alone (34 by 34,
// radius 8), or an icon and a word (32 high, 10px in, 13px text 7px after
// a 16px icon). The tool in use has the neutral selected fill with its
// icon in the accent. `primary` is the accent Capture and Save button
// (16px in, semibold, 15px icon 8px before the word).
T.Button {
    id: control

    property string iconName
    property real iconSize: tool ? 17 : 16
    property bool tool: false
    property bool active: false
    property bool primary: false
    property bool dropDown: false
    property string tip

    readonly property bool iconOnly: text.length === 0
    readonly property bool lit: hovered && enabled

    implicitWidth: tool ? 36 : iconOnly ? 34 : row.implicitWidth + (primary ? 32 : 20)
    implicitHeight: tool ? 32 : iconOnly ? 34 : 32
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    Accessible.name: iconOnly ? tip : text
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: control.tool || !control.iconOnly ? UI.Style.radiusXs : UI.Style.radiusSm
        color: control.primary ? (control.down ? UI.Style.accentPressed : control.lit ? UI.Style.accentHover : UI.Style.accent)
            : control.down ? UI.Style.surfacePressed : control.active ? UI.Style.surfaceSelected : control.lit ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.primary ? 8 : control.dropDown ? 6 : 7

            CaptureIcon {
                visible: control.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: control.primary ? 15 : control.iconSize
                strokeWidth: control.tool ? 1.8 : 2
                color: control.primary ? UI.Style.textOnAccent : control.active ? UI.Style.accent : UI.Style.iconInk
            }

            UI.Label {
                visible: !control.iconOnly
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                size: control.dropDown ? 12.5 : 13
                font.weight: control.primary ? UI.Style.weightSemiBold : UI.Style.weightRegular
                color: control.primary ? UI.Style.textOnAccent : UI.Style.ink
            }

            CaptureIcon {
                visible: control.dropDown
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                size: 10
                strokeWidth: 2.4
                color: UI.Style.inkSubtle
            }
        }
    }

    UI.Tooltip {
        text: control.tip
        visible: control.hovered && control.tip.length > 0 && !control.down
    }
}
