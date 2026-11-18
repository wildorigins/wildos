import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// One of the status shortcuts beside the PC name on Home (Network, Updates):
// a line icon, what it is and how it stands, opening its page.
T.AbstractButton {
    id: link

    property string iconName
    property color iconColor: UI.Style.iconInk
    property string title
    property string caption

    implicitWidth: 230
    implicitHeight: Math.max(64, column.implicitHeight + 20)
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("%1, %2").arg(title).arg(caption)

    background: Rectangle {
        radius: UI.Style.radiusMd
        color: link.down ? UI.Style.surfacePressed : link.hovered ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: link.visualFocus
            targetRadius: UI.Style.radiusMd
        }
    }

    contentItem: Item {
        UI.Icon {
            id: icon
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: link.iconName
            size: 20
            strokeWidth: 1.8
            color: link.iconColor
        }

        Column {
            id: column
            anchors.left: icon.right
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            UI.Label {
                width: parent.width
                text: link.title
                size: 13.5
                font.weight: UI.Style.weightSemiBold
            }

            UI.Label {
                width: parent.width
                text: link.caption
                size: 12
                color: UI.Style.inkMuted
            }
        }
    }
}
