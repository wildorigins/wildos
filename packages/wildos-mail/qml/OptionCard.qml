import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// One of the cards down an open task (MailTasks.dc): an icon, what is set
// or what can be, a second line if there is one, and a cross to clear it.
Rectangle {
    id: card

    property string iconName
    property string title
    property string detail
    property color titleColor: UI.Style.ink
    property bool clearable: false

    signal clicked()
    signal cleared()

    Layout.fillWidth: true
    implicitHeight: Math.max(42, column.implicitHeight + 20)
    radius: UI.Style.radiusSm
    color: mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceRaised
    border.width: 1
    border.color: UI.Style.borderSubtle
    Accessible.role: Accessible.Button
    Accessible.name: detail ? title + ", " + detail : title

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.clicked()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 6
        spacing: 12
        MailIcon {
            name: card.iconName
            size: 17
            strokeWidth: 1.8
            color: UI.Style.iconInk
        }
        ColumnLayout {
            id: column
            Layout.fillWidth: true
            spacing: 1
            UI.Label {
                Layout.fillWidth: true
                text: card.title
                size: 13
                color: card.titleColor
                elide: Text.ElideRight
            }
            UI.Label {
                Layout.fillWidth: true
                visible: card.detail.length > 0
                text: card.detail
                size: 12
                color: UI.Style.inkMuted
                elide: Text.ElideRight
            }
        }
        UI.IconButton {
            id: clearButton
            visible: card.clearable && (mouse.containsMouse || clearButton.hovered)
            implicitWidth: 28
            implicitHeight: 28
            iconName: "close"
            iconSize: 13
            tip: qsTr("Remove")
            onClicked: card.cleared()
        }
    }
}
