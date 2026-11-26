import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// A row in the sidebar (AppNotes.dc): 34px, 8px in from each side, an icon,
// the name and a count. The open one has the neutral selected fill, a
// semibold name and the small accent bar at its left edge.
Item {
    id: row

    property string iconName
    property string text
    property int count: -1
    property bool current: false

    signal clicked()
    signal menuRequested(real x, real y)

    implicitHeight: 34
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: count >= 0 ? qsTr("%1, %2").arg(text).arg(count) : text
    Accessible.selected: current
    Accessible.onPressAction: row.clicked()
    Keys.onReturnPressed: row.clicked()
    Keys.onEnterPressed: row.clicked()
    Keys.onSpacePressed: row.clicked()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            row.menuRequested(18, row.height - 4);
            event.accepted = true;
        }
    }

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

        // Drawn inside the row: the usual ring sits outside its control,
        // where the sidebar's scrolling would clip it on the first row.
        Rectangle {
            anchors.fill: parent
            visible: row.activeFocus
            radius: parent.radius
            color: "transparent"
            border.width: 2
            border.color: UI.Style.accent
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 11

        NotesIcon {
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
        UI.Label {
            visible: row.count >= 0
            text: row.count
            size: 12
            color: UI.Style.inkSubtle
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function (event) {
            if (event.button === Qt.RightButton) {
                row.menuRequested(event.x, event.y);
            } else {
                row.clicked();
            }
        }
    }
}
