import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// The way to the Windows program itself, from StoreSearch.dc.html's
// Windows apps row: the window tile, what Wild OS can do, and its
// settings.
Item {
    id: row

    property string title: qsTr("Need the Windows app itself?")

    Layout.fillWidth: true
    implicitHeight: Math.max(56, texts.implicitHeight) + 32

    Rectangle {
        id: tile
        anchors.verticalCenter: parent.verticalCenter
        width: 56
        height: 56
        radius: 13
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: "#4a4c50" }
            GradientStop { position: 1; color: "#232529" }
        }

        StoreIcon {
            anchors.centerIn: parent
            name: "windows-apps"
            size: 26
            color: "#cfd9ff"
        }
    }

    Column {
        id: texts
        anchors.left: tile.right
        anchors.leftMargin: 16
        anchors.right: action.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        UI.Label {
            width: parent.width
            text: row.title
            size: 14.5
            font.weight: UI.Style.weightSemiBold
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.Label {
            width: parent.width
            text: qsTr("Wild OS can run many Windows programs in a sandbox. Open the .exe file from Files and Wild OS will offer to install it.")
            size: 13
            lineHeight: 1.45
            color: Metrics.inkSecondary
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    PillButton {
        id: action
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Windows app settings")
        onClicked: Catalog.openSettings("windowsapps")
    }
}
