import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// One message in the list (MailMicrosoft.dc): the sender's initials, name,
// subject and time, and a line of preview. Unread mail is bold with its
// a small accent bar at the left edge; the text stays plain ink.
Item {
    id: row

    required property int index
    required property string key
    required property string from
    required property string initials
    required property int avatar
    required property string subject
    required property string preview
    required property string time
    required property bool unread
    required property bool flagged
    required property bool pinned
    required property bool attachment
    required property bool draft
    required property string folderName
    required property int threadCount

    property bool showFolder: false
    property string highlight

    signal selected(string key)
    signal menuRequested(string key, real x, real y)
    signal openRequested(string key)

    readonly property bool current: MailEngine.selection.indexOf(key) >= 0
    readonly property bool hovered: mouse.containsMouse || actions.hovered

    // Wraps each match in the search in bold (MailMsSearch.dc).
    function marked(text) {
        const plain = text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        if (highlight.length === 0) {
            return plain;
        }
        const escaped = highlight.replace(/[.*+?^${}()|[\]\\]/g, "\\$&").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        return plain.replace(new RegExp("(" + escaped + ")", "ig"), "<b>$1</b>");
    }

    implicitHeight: content.implicitHeight + 18
    Accessible.role: Accessible.ListItem
    Accessible.name: unread ? qsTr("Unread, %1, %2, %3").arg(from).arg(subject).arg(time) : qsTr("%1, %2, %3").arg(from).arg(subject).arg(time)
    Accessible.selected: current

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        radius: UI.Style.radiusXs
        color: row.current ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        Rectangle {
            visible: row.unread
            x: 0
            y: 8
            width: 3
            height: parent.height - 16
            radius: 2
            color: UI.Style.accent
        }
    }

    // What a dragged message carries to a folder: every selected message
    // if it is one of them, otherwise just itself.
    Item {
        id: dragProxy
        width: 1
        height: 1
        property bool dragging: false
        Drag.active: dragging
        Drag.keys: ["wildos-mail-messages"]
        Drag.mimeData: ({ "text/plain": row.subject })
        Drag.hotSpot.x: 0
        Drag.hotSpot.y: 0
        property var keys: MailEngine.selection.indexOf(row.key) >= 0 ? MailEngine.selection : [row.key]
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Without this the list takes the pointer to scroll, and a message
        // can't be dragged to a folder.
        preventStealing: true
        property point pressedAt
        onPressed: function (event) {
            pressedAt = Qt.point(event.x, event.y);
        }
        // The dragged point follows the pointer exactly, so the folder under
        // the pointer is the one that takes the drop.
        onPositionChanged: function (event) {
            if (!pressed) {
                return;
            }
            dragProxy.x = event.x;
            dragProxy.y = event.y;
            if (!dragProxy.dragging && Math.abs(event.x - pressedAt.x) + Math.abs(event.y - pressedAt.y) > 10) {
                dragProxy.dragging = true;
            }
        }
        onReleased: {
            if (dragProxy.dragging) {
                dragProxy.Drag.drop();
                dragProxy.dragging = false;
            }
        }
        onCanceled: dragProxy.dragging = false
        onClicked: function (event) {
            if (dragProxy.dragging) {
                return;
            }
            if (event.button === Qt.RightButton) {
                if (MailEngine.selection.indexOf(row.key) < 0) {
                    row.selected(row.key);
                }
                row.menuRequested(row.key, event.x, event.y);
            } else if (event.modifiers & Qt.ControlModifier) {
                MailEngine.toggleSelect(row.key);
            } else if (event.modifiers & Qt.ShiftModifier) {
                MailEngine.extendSelect(row.key);
            } else {
                row.selected(row.key);
            }
        }
        onDoubleClicked: function (event) {
            if (event.button === Qt.LeftButton) {
                row.openRequested(row.key);
            }
        }
    }

    // Outlook's quick actions, shown while the pointer is over the message.
    Row {
        id: actions
        property bool hovered: hoverActions.hovered
        visible: row.hovered && !row.draft
        z: 2
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 6
        spacing: 2

        HoverHandler {
            id: hoverActions
        }
        Rectangle {
            width: actionRow.implicitWidth + 4
            height: 30
            radius: 7
            color: UI.Style.windowOpaque
            border.width: 1
            border.color: UI.Style.borderSubtle

            Row {
                id: actionRow
                anchors.centerIn: parent
                spacing: 0
                CommandButton {
                    iconOnly: true
                    iconName: "trash"
                    tip: qsTr("Delete")
                    implicitWidth: 28
                    implicitHeight: 26
                    onClicked: MailEngine.removeKeys([row.key])
                }
                CommandButton {
                    iconOnly: true
                    iconName: "read-unread"
                    tip: row.unread ? qsTr("Mark as read") : qsTr("Mark as unread")
                    implicitWidth: 28
                    implicitHeight: 26
                    onClicked: MailEngine.setReadKeys([row.key], row.unread)
                }
                CommandButton {
                    iconOnly: true
                    iconName: "flag"
                    tip: row.flagged ? qsTr("Clear flag") : qsTr("Flag")
                    implicitWidth: 28
                    implicitHeight: 26
                    onClicked: MailEngine.setFlaggedKeys([row.key], !row.flagged)
                }
                CommandButton {
                    iconOnly: true
                    iconName: "pin"
                    tip: row.pinned ? qsTr("Unpin") : qsTr("Pin")
                    implicitWidth: 28
                    implicitHeight: 26
                    onClicked: MailEngine.togglePin(row.key)
                }
            }
        }
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.rightMargin: 18
        anchors.topMargin: 9
        spacing: 11

        SenderBadge {
            Layout.alignment: Qt.AlignTop
            initials: row.initials
            colourIndex: row.avatar
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                UI.Label {
                    Layout.fillWidth: true
                    text: row.draft ? qsTr("[Draft] ") + row.from : row.from
                    size: 13.5
                    font.weight: row.unread ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: row.draft ? UI.Style.danger : UI.Style.ink
                    elide: Text.ElideRight
                }
                UI.Label {
                    visible: row.threadCount > 1
                    text: row.threadCount
                    size: 11.5
                    color: UI.Style.inkMuted
                }
                MailIcon {
                    visible: row.pinned
                    name: "pin"
                    size: 14
                    color: UI.Style.inkMuted
                }
                MailIcon {
                    visible: row.attachment
                    name: "paperclip"
                    size: 14
                    color: UI.Style.inkMuted
                }
                MailIcon {
                    visible: row.flagged
                    name: "flag"
                    size: 14
                    color: UI.Style.danger
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                UI.Label {
                    Layout.fillWidth: true
                    text: row.marked(row.subject)
                    textFormat: Text.StyledText
                    size: 13
                    font.weight: row.unread ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: UI.Style.ink
                    elide: Text.ElideRight
                }
                UI.Label {
                    text: row.time
                    size: 12
                    color: UI.Style.inkMuted
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.showFolder ? row.folderName + "  " + row.preview : row.preview
                size: 12.5
                color: UI.Style.inkMuted
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }
}
