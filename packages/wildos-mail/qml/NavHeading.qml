import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// A heading in the left column of People and Tasks, Groups or Lists, that
// folds its rows away.
Item {
    id: heading

    property string text
    property bool expanded: true

    implicitHeight: 36
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: text
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            heading.expanded = !heading.expanded;
            event.accepted = true;
        }
    }

    Item {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 4
        visible: heading.activeFocus

        UI.FocusRing {
            anchors.margins: 0
            targetRadius: UI.Style.radiusXs - 4
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: 4
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8
        MailIcon {
            name: heading.expanded ? "chevron-down" : "chevron-right"
            size: 13
            color: UI.Style.iconMuted
        }
        UI.Label {
            Layout.fillWidth: true
            text: heading.text
            size: 13
            font.weight: UI.Style.weightSemiBold
            elide: Text.ElideRight
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: heading.expanded = !heading.expanded
    }
}
