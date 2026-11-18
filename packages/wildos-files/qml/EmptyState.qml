import QtQuick
import WildOS.Files
import WildOS.UI as UI

// What the content area says when it has nothing to show.
Item {
    id: empty

    required property var folder
    readonly property string message: {
        if (!folder) {
            return "";
        }
        if (folder.errorText.length > 0) {
            return folder.errorText;
        }
        if (folder.count > 0 || (folder.loading && folder.searchText.length === 0)) {
            return "";
        }
        if (folder.searchText.length > 0) {
            return folder.searching ? qsTr("Searching…") : qsTr("No items match your search.");
        }
        if (folder.kind === "bin") {
            return qsTr("The Recycle Bin is empty.");
        }
        return qsTr("This folder is empty.");
    }
    // A folder that is slow to list (a network place, a drive waking up)
    // says so, rather than looking empty. Most folders list before this
    // would show.
    readonly property bool waiting: !!folder && folder.loading && folder.count === 0 && folder.errorText.length === 0 && folder.searchText.length === 0
    property bool late: false
    // What the system reported for an error Files only has a general
    // sentence for, shown once "Details" is clicked.
    readonly property string detail: folder && folder.errorText.length > 0 ? folder.errorDetail : ""
    property bool detailShown: false

    visible: message.length > 0 || late
    onWaitingChanged: if (!waiting) late = false
    onDetailChanged: detailShown = false

    Timer {
        interval: 300
        running: empty.waiting
        onTriggered: empty.late = true
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 36
        width: Math.min(parent.width - 48, 420)
        spacing: 6

        UI.Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: empty.message
            color: UI.Style.inkMuted
        }

        UI.LinkButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: empty.detail.length > 0 && !empty.detailShown
            text: qsTr("Details")
            onClicked: empty.detailShown = true
        }

        UI.Label {
            width: parent.width
            visible: empty.detailShown
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            text: empty.detail
            size: 12
            color: UI.Style.inkMuted
        }
    }

    Row {
        visible: empty.late
        anchors.horizontalCenter: parent.horizontalCenter
        y: 36
        spacing: 10

        UI.Spinner {
            anchors.verticalCenter: parent.verticalCenter
            size: 16
        }

        UI.Label {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Working on it…")
            color: UI.Style.inkMuted
        }
    }
}
