import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The left column (AppBackup.dc): the five pages, and at the foot the
// promise the app keeps: nothing leaves this PC.
Rectangle {
    id: sidebar

    property string page

    signal navigate(string page)

    color: BackupStyle.sideShade

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: BackupStyle.hairline
    }

    Column {
        anchors.top: parent.top
        anchors.topMargin: 4
        width: parent.width - 1

        Repeater {
            model: [
                { page: "overview", icon: "overview", text: qsTr("Overview") },
                { page: "restore", icon: "history", text: qsTr("Restore files") },
                { page: "schedule", icon: "schedule", text: qsTr("Schedule") },
                { page: "drives", icon: "drive", text: qsTr("Drives") },
                { page: "settings", icon: "sliders", text: qsTr("Settings") }
            ]
            delegate: NavRow {
                required property var modelData
                width: parent.width
                iconName: modelData.icon
                text: modelData.text
                current: sidebar.page === modelData.page
                onClicked: sidebar.navigate(modelData.page)
            }
        }
    }

    // Sized to its words: on the board the second line spilled out.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 10
        anchors.rightMargin: 11
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        height: cardRow.implicitHeight + 22
        radius: UI.Style.radiusPanel
        color: BackupStyle.card
        border.width: 1
        border.color: BackupStyle.hairline
        Accessible.role: Accessible.StaticText
        Accessible.name: cardTitle.text + ". " + cardText.text

        RowLayout {
            id: cardRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 11
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            BackupIcon {
                name: "shield-check"
                size: 18
                color: BackupStyle.good
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    id: cardTitle
                    Layout.fillWidth: true
                    text: qsTr("Nothing leaves this PC")
                    size: 12
                    font.weight: UI.Style.weightSemiBold
                    wrapMode: Text.Wrap
                }
                UI.Label {
                    id: cardText
                    Layout.fillWidth: true
                    text: qsTr("Backups go to your own drive")
                    size: 11
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
