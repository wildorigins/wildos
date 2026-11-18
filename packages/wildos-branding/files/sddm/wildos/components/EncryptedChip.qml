import QtQuick
import Qt.labs.folderlistmodel
import WildOS.UI as UI

// "Encrypted and locked" is a promise, so it only shows when the installer has
// recorded that the system disk really is encrypted, by creating the file
// /etc/wildos/encrypted. QML cannot stat a file directly; a folder model
// filtered to that one name gives a count of 0 or 1 instead, and a missing
// /etc/wildos simply counts as 0.
Item {
    id: chip

    property Item backdrop: null
    readonly property bool encrypted: marker.status === FolderListModel.Ready && marker.count > 0

    implicitWidth: row.implicitWidth + 28
    implicitHeight: 40
    visible: opacity > 0
    opacity: encrypted ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 300 } }

    Accessible.role: Accessible.StaticText
    Accessible.name: label.text

    FolderListModel {
        id: marker
        folder: "file:///etc/wildos"
        nameFilters: ["encrypted"]
        showDirs: false
        showHidden: true
        showDotAndDotDot: false
    }

    Glass {
        anchors.fill: parent
        backdrop: chip.backdrop
        tint: UI.Style.glassOnWallpaper
        blurRadius: 24
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        UI.Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "shield-check"
            size: 16
            strokeWidth: 2.2
            color: UI.Style.accentText
        }

        UI.Label {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Encrypted and locked")
            color: UI.Style.inkOnWallpaper
        }
    }
}
