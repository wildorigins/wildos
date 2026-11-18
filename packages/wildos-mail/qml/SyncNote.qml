import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The card at the bottom of People's and Tasks' left column: where the
// contacts or tasks are kept, as Mail's says what it kept out.
Rectangle {
    id: note

    property string iconName: "shield-check"
    property color iconColor: UI.Style.success
    property string title
    property string detail

    signal clicked()

    implicitHeight: content.implicitHeight + 22
    radius: UI.Style.radiusPanel
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: note.clicked()
    }

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 11
        anchors.leftMargin: 12
        spacing: 10

        MailIcon {
            name: note.iconName
            size: 18
            color: note.iconColor
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            UI.Label {
                Layout.fillWidth: true
                text: note.title
                size: 12
                font.weight: UI.Style.weightSemiBold
                elide: Text.ElideRight
            }
            UI.Label {
                Layout.fillWidth: true
                text: note.detail
                size: 11
                color: UI.Style.inkMuted
                elide: Text.ElideRight
            }
        }
    }
}
