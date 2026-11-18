import QtQuick
import WildOS.UI as UI

// A card on Backup's pages (AppBackup.dc): radius 10, the raised fill, a
// hairline edge and 16 by 18 of padding round what it holds.
Rectangle {
    id: panel

    default property alias content: holder.data
    property real padding: 16
    property real sidePadding: 18

    implicitHeight: holder.childrenRect.height + padding * 2
    radius: UI.Style.radiusPanel
    color: BackupStyle.card
    border.width: 1
    border.color: BackupStyle.hairline

    Item {
        id: holder
        anchors.fill: parent
        anchors.topMargin: panel.padding
        anchors.bottomMargin: panel.padding
        anchors.leftMargin: panel.sidePadding
        anchors.rightMargin: panel.sidePadding
    }
}
