import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// The board's check box: 18px at radius 5, a 1.5px edge when clear, the
// accent with a 13px tick when ticked, and 10px to its label.
T.CheckBox {
    id: control

    readonly property bool lit: hovered && enabled

    implicitWidth: 18 + (text.length > 0 ? spacing + label.implicitWidth : 0)
    implicitHeight: Math.max(22, label.implicitHeight)
    spacing: 10
    padding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    Accessible.name: text

    indicator: Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        radius: 5
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.lit
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: control.checked ? (control.lit ? UI.Style.accentHover : UI.Style.accent) : control.lit ? ArchiveStyle.rowHover : "transparent"
        border.width: control.checked ? 0 : 1.5
        border.color: ArchiveStyle.checkEdge

        ArchiveIcon {
            visible: control.checked
            anchors.centerIn: parent
            name: "check"
            size: 13
            strokeWidth: 3
            color: UI.Style.textOnAccent
        }

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: 5
        }
    }

    contentItem: UI.Label {
        id: label
        visible: control.text.length > 0
        leftPadding: 18 + control.spacing
        text: control.text
        size: 13
        verticalAlignment: Text.AlignVCenter
    }
}
