import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// One run in Recent backups (AppBackup.dc): a 16px ringed tick, green when
// it went as planned, amber when it caught up late; red for one that
// failed. When, and what happened.
Rectangle {
    id: row

    property var entry: ({})

    readonly property bool done: entry.result === "done"
    readonly property bool caughtUp: done && entry.missedAt !== undefined
    readonly property date at: new Date(caughtUp ? entry.missedAt : entry.at)

    function detail() {
        if (entry.result === "failed") {
            return entry.reason || qsTr("Didn't finish");
        }
        if (entry.result === "stopped") {
            return qsTr("Stopped");
        }
        if (caughtUp) {
            const finished = BackupStyle.clock(new Date(entry.finished || entry.at));
            return entry.missedWhy === "drive" ? qsTr("Drive unplugged, caught up at %1").arg(finished) : qsTr("Caught up at %1").arg(finished);
        }
        if (entry.dataAdded !== undefined) {
            return qsTr("%1 changed").arg(BackupStyle.size(entry.dataAdded));
        }
        return qsTr("Backed up");
    }

    implicitHeight: layout.implicitHeight + 20
    radius: 8
    color: mouse.containsMouse ? BackupStyle.rowHover : "transparent"
    Accessible.role: Accessible.ListItem
    Accessible.name: when.text + ". " + what.text

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
    }

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        BackupIcon {
            name: row.done ? "circle-check" : row.entry.result === "failed" ? "circle-alert" : "circle-stop"
            size: 16
            color: row.caughtUp ? BackupStyle.late : row.done ? BackupStyle.good : row.entry.result === "failed" ? BackupStyle.bad : UI.Style.inkSubtle
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            UI.Label {
                id: when
                Layout.fillWidth: true
                text: BackupStyle.when(row.at)
                size: 13
                elide: Text.ElideRight
            }
            UI.Label {
                id: what
                Layout.fillWidth: true
                text: row.detail()
                size: 12
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            ErrorDetails {
                Layout.fillWidth: true
                text: row.entry.result === "failed" ? (row.entry.detail || "") : ""
            }
        }
    }
}
