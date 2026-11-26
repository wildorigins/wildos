import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// A row in the side column (AppBackup.dc): 34px, 8px in from each side, a
// 16px icon and the page's name. The open one has the neutral selected
// fill, a semibold name and the accent bar at its left edge.
Item {
    id: row

    property string iconName
    property string text
    property bool current: false

    signal clicked()

    implicitHeight: 34
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: current
    Keys.onReturnPressed: row.clicked()
    Keys.onSpacePressed: row.clicked()

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        radius: UI.Style.radiusXs
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: mouse.containsMouse
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.current ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        Rectangle {
            visible: row.current
            x: 0
            y: 9
            width: 3
            height: parent.height - 18
            radius: 2
            color: UI.Style.accent
        }

        UI.FocusRing {
            visible: row.activeFocus
            targetRadius: UI.Style.radiusXs
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 11

        BackupIcon {
            name: row.iconName
            size: 16
            color: row.current ? UI.Style.iconInk : UI.Style.inkMuted
        }
        UI.Label {
            Layout.fillWidth: true
            text: row.text
            size: 13
            font.weight: row.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }
}
