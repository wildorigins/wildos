import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// One note in the list (AppNotes.dc): an optional colour dot, the title,
// a pin when pinned and the date, then a line of the text. 8px in from the
// sides, 12 by 14 inside, radius 8; the open note has the neutral selected
// fill, the others a hairline under them.
Item {
    id: row

    required property int index
    required property string path
    required property string title
    required property string preview
    required property bool pinned
    required property string colour
    required property string dateText

    property bool current: false

    signal selected(string path)
    signal menuRequested(string path, real x, real y)

    implicitHeight: content.implicitHeight + 24
    Accessible.role: Accessible.ListItem
    Accessible.name: (pinned ? qsTr("Pinned, ") : "") + title + ", " + dateText + ". " + preview
    Accessible.selected: current

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        radius: UI.Style.radiusSm
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: mouse.containsMouse
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.current ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        Rectangle {
            visible: !row.current
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: UI.Style.borderSubtle
        }
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                visible: NoteColours.known(row.colour)
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: NoteColours.colourOf(row.colour)
            }
            UI.Label {
                Layout.fillWidth: true
                text: row.title
                size: 13.5
                font.weight: UI.Style.weightSemiBold
            }
            NotesIcon {
                visible: row.pinned
                name: "pin"
                size: 13
                color: UI.Style.inkMuted
            }
            UI.Label {
                text: row.dateText
                size: 12
                color: UI.Style.inkSubtle
            }
        }
        UI.Label {
            Layout.fillWidth: true
            text: row.preview.length > 0 ? row.preview : qsTr("No text yet")
            size: 12.5
            color: row.preview.length > 0 ? UI.Style.inkMuted : UI.Style.inkSubtle
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function (event) {
            row.selected(row.path);
            if (event.button === Qt.RightButton) {
                row.menuRequested(row.path, event.x, event.y);
            }
        }
    }
}
