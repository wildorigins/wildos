import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A navigation entry: 38px, line icon and label. The current one gets the
// selected fill, a short accent bar and an accent icon, as in the design;
// the sidebar items take the token radius 8.
T.AbstractButton {
    id: item

    property string iconName
    property bool current: false

    implicitHeight: Math.max(38, label.implicitHeight + 16)
    width: parent ? parent.width : implicitWidth
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: current

    background: Rectangle {
        radius: UI.Style.radiusSm
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: item.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: item.down ? UI.Style.surfacePressed : item.hovered ? UI.Style.surfaceHover : item.current ? UI.Style.surfaceSelected : UI.Style.surfaceHoverClear

        Rectangle {
            visible: item.current
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 16
            radius: 2
            color: UI.Style.accent
        }

        UI.FocusRing {
            visible: item.visualFocus
            anchors.margins: -2
            targetRadius: UI.Style.radiusSm
        }
    }

    contentItem: Item {
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: 12
            spacing: 12

            UI.Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: item.iconName
                size: 17
                strokeWidth: item.current ? 1.9 : 1.8
                color: item.current ? UI.Style.accent : UI.Style.iconInk
            }

            UI.Label {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                font.weight: item.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
            }
        }
    }
}
