import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// A row in the left column of People and Tasks (MailPeople.dc,
// MailTasks.dc): 32px, an icon, the name and a count. The one open has the
// neutral selected fill and semibold name, as Mail's folders do.
Item {
    id: row

    property string iconName
    property string text
    property int count: 0
    property bool current: false
    property bool showCount: true
    // Something dragged here can drop (a task on a list).
    property bool highlighted: false

    signal clicked()
    // `keyboard` is true when the Menu key asked, so the menu can open
    // beside the row and not wherever the pointer was left.
    signal menuRequested(bool keyboard)

    implicitHeight: 32
    activeFocusOnTab: true
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            row.clicked();
        } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            row.menuRequested(true);
        } else {
            return;
        }
        event.accepted = true;
    }
    Accessible.role: Accessible.ListItem
    Accessible.name: showCount && count > 0 ? qsTr("%1, %2").arg(text).arg(count) : text
    Accessible.selected: current

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        radius: UI.Style.radiusXs
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: mouse.containsMouse
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.current || row.highlighted ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        // Drawn inside the row: the rows touch.
        UI.FocusRing {
            visible: row.activeFocus
            anchors.margins: 0
            targetRadius: parent.radius - 4
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 16
        spacing: 10

        MailIcon {
            name: row.iconName
            size: 16
            color: row.current ? UI.Style.iconInk : UI.Style.iconMuted
        }
        UI.Label {
            Layout.fillWidth: true
            text: row.text
            size: 13
            font.weight: row.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
            elide: Text.ElideRight
        }
        UI.Label {
            visible: row.showCount && row.count > 0
            text: row.count
            size: 12
            color: UI.Style.inkMuted
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
                row.menuRequested(false);
            } else {
                row.clicked();
            }
        }
    }
}
