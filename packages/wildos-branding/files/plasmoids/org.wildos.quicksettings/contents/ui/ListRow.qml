import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// One row in a detail page list: an icon, a name and a status line. The row
// in use is tinted like the design's highlighted notification. Rows can open
// to show buttons or a password field underneath, which WildOS.UI's ListRow
// does not do, so this one stays local on the shared tokens.
FocusScope {
    id: row

    property string title
    property string subtitle
    property bool active: false
    property bool expanded: false
    property Component icon
    default property alias details: detailsColumn.data

    signal clicked()

    implicitHeight: head.height + (expanded ? detailsColumn.implicitHeight + 12 : 0)
    Behavior on implicitHeight {
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusMd
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: head.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.active ? UI.Style.accentSoft
            : head.down ? UI.Style.surfacePressed
            : head.hovered || row.expanded ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        border.width: row.active ? 1 : 0
        border.color: UI.Style.accentBorder
    }

    T.AbstractButton {
        id: head
        width: parent.width
        height: Math.max(52, texts.implicitHeight + 16)
        padding: 0
        hoverEnabled: true
        focus: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Button
        Accessible.name: row.title
        Accessible.description: row.subtitle
        onClicked: row.clicked()

        background: Item {
            UI.FocusRing {
                visible: head.visualFocus || (head.activeFocus && Nav.keyboard)
                targetRadius: UI.Style.radiusMd
            }
        }

        contentItem: Item {
            Loader {
                id: iconSlot
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                sourceComponent: row.icon
            }

            ColumnLayout {
                id: texts
                anchors.left: iconSlot.right
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: row.title
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: row.subtitle
                    size: 12
                    color: UI.Style.inkMuted
                }
            }
        }
    }

    // Clipped so the details slide open with the row instead of spilling
    // over the rows below while it grows.
    Item {
        y: head.height
        width: parent.width
        height: Math.max(0, row.height - head.height)
        clip: true
        visible: row.expanded || height > 0

        ColumnLayout {
            id: detailsColumn
            x: 44
            width: parent.width - 44 - 12
            spacing: 8
        }
    }
}
