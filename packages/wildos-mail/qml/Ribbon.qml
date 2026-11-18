import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The ribbon of People and Tasks (MailPeople.dc, MailTasks.dc), drawn as
// Mail's and Calendar's: Home, View and Help over a 46px card. Each tab's
// commands are a row inside it, shown while `current` is that tab.
ColumnLayout {
    id: ribbon

    property var tabs: [{ id: "home", name: qsTr("Home") }, { id: "view", name: qsTr("View") }, { id: "help", name: qsTr("Help") }]
    property string current: "home"
    default property alias content: card.data

    spacing: 2

    Row {
        leftPadding: 4
        spacing: 22

        Repeater {
            model: ribbon.tabs
            delegate: Item {
                id: tab
                required property var modelData
                readonly property bool active: ribbon.current === modelData.id
                width: tabLabel.implicitWidth + 4
                height: 36
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.name

                UI.Label {
                    id: tabLabel
                    anchors.centerIn: parent
                    text: tab.modelData.name
                    size: 13
                    font.weight: tab.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: tab.active ? UI.Style.ink : UI.Style.inkMuted
                }
                Rectangle {
                    visible: tab.active
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    height: 2
                    radius: 1
                    color: UI.Style.accent
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ribbon.current = tab.modelData.id
                }
            }
        }
    }

    Rectangle {
        id: card
        Layout.fillWidth: true
        implicitHeight: 46
        radius: UI.Style.radiusPanel
        color: UI.Style.surfaceRest
        border.width: 1
        border.color: UI.Style.borderSubtle
        clip: true
    }
}
