import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// A PDF opened lately, on the empty window: its name over its folder, and
// a cross to take it off the list.
Item {
    id: row

    property string name
    property string folder

    signal clicked()
    signal removeRequested()

    implicitHeight: 48
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: name
    Accessible.description: folder
    Accessible.onPressAction: row.clicked()
    Keys.onReturnPressed: row.clicked()
    Keys.onEnterPressed: row.clicked()
    Keys.onSpacePressed: row.clicked()
    Keys.onDeletePressed: row.removeRequested()

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusXs
        color: mouse.pressed ? UI.Style.surfacePressed : mouse.containsMouse || remove.hovered ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: row.activeFocus
            targetRadius: UI.Style.radiusXs
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 6
        spacing: 12

        Image {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            source: "qrc:/qt/qml/WildOS/PDF/data/wildos-pdf.svg"
            sourceSize.width: 44
            sourceSize.height: 44
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            UI.Label {
                Layout.fillWidth: true
                text: row.name
                size: 13
                elide: Text.ElideMiddle
            }
            UI.Label {
                Layout.fillWidth: true
                text: row.folder
                size: 12
                color: UI.Style.inkSubtle
                elide: Text.ElideLeft
            }
        }
        ToolButton {
            id: remove
            iconName: "x"
            iconSize: 14
            tip: qsTr("Remove from the list")
            opacity: mouse.containsMouse || hovered || activeFocus || row.activeFocus ? 1 : 0
            onClicked: row.removeRequested()
        }
    }
}
