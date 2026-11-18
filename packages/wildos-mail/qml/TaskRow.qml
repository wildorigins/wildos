import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// One task in the middle of Tasks (MailTasks.dc): the tick, the title, what
// list it is in and when it is due, and the star. A flagged message shows
// the same way, as "From email: subject".
Rectangle {
    id: card

    property var task: ({})
    property bool fromMail: false
    property bool current: false
    property bool showList: true
    property bool showMyDay: true

    signal picked()
    signal menuRequested()
    signal ticked()
    signal starred()

    // A piece of the line under the title, after a dot.
    component Segment: Row {
        id: segment
        property string iconName
        property string text
        property color color: UI.Style.inkMuted
        property bool first: false
        spacing: 4
        UI.Label {
            visible: !segment.first
            text: "·"
            size: 12
            color: UI.Style.inkMuted
            opacity: 0.5
        }
        MailIcon {
            visible: segment.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: segment.iconName
            size: 12
            strokeWidth: 2
            color: segment.color
        }
        UI.Label {
            visible: segment.text.length > 0
            text: segment.text
            size: 12
            color: segment.color
        }
    }

    implicitHeight: content.implicitHeight + 20
    radius: UI.Style.radiusSm
    color: current ? UI.Style.surfaceSelected : rowMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceRaised
    border.width: 1
    border.color: UI.Style.borderSubtle
    Accessible.role: Accessible.ListItem
    Accessible.name: fromMail ? qsTr("From email: %1").arg(task.subject || "") : task.title || ""

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function (event) {
            if (event.button === Qt.RightButton) {
                card.menuRequested();
            } else {
                card.picked();
            }
        }
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 14

        Item {
            implicitWidth: 22
            implicitHeight: 22
            MailIcon {
                anchors.centerIn: parent
                name: card.task.done ? "check-circle" : "circle"
                size: 20
                strokeWidth: 1.6
                color: card.task.done || tickMouse.containsMouse ? UI.Style.accent : UI.Style.iconMuted
            }
            MouseArea {
                id: tickMouse
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                enabled: card.fromMail || !card.task.readOnly
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.CheckBox
                Accessible.name: card.task.done ? qsTr("Mark as not done") : qsTr("Mark complete")
                onClicked: card.ticked()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            UI.Label {
                Layout.fillWidth: true
                text: card.fromMail ? qsTr("From email: %1").arg(card.task.subject || "") : card.task.title || ""
                size: 13.5
                font.strikeout: card.task.done === true
                color: card.task.done ? UI.Style.inkMuted : UI.Style.ink
                elide: Text.ElideRight
            }
            Row {
                visible: !card.task.done && segments.count > 0
                spacing: 4
                Repeater {
                    id: segments
                    model: {
                        const d = card.task;
                        const out = [];
                        if (card.fromMail) {
                            out.push({ icon: "", text: d.from || "" });
                            out.push({ icon: "mail", text: d.date ? Formats.shortDayAndMonth(new Date(d.date)) : "" });
                            return out;
                        }
                        if (card.showList && d.listName) {
                            out.push({ icon: "", text: d.listName });
                        }
                        if (d.myDay && card.showMyDay) {
                            out.push({ icon: "sun", text: qsTr("My day") });
                        }
                        if (d.dueText) {
                            out.push({ icon: "calendar", text: d.dueText, danger: d.dueState !== "" });
                        }
                        if (d.steps > 0) {
                            out.push({ icon: "notes", text: qsTr("%1 of %2").arg(d.stepsDone).arg(d.steps) });
                        }
                        if (d.files > 0) {
                            out.push({ icon: "paperclip", text: d.files === 1 ? qsTr("1 file") : qsTr("%1 files").arg(d.files) });
                        }
                        if (d.repeat && d.repeat !== "none") {
                            out.push({ icon: "repeat", text: "" });
                        }
                        if (d.reminder) {
                            out.push({ icon: "bell", text: "" });
                        }
                        return out;
                    }
                    delegate: Segment {
                        required property var modelData
                        required property int index
                        first: index === 0
                        iconName: modelData.icon
                        text: modelData.text
                        color: modelData.danger ? UI.Style.danger : UI.Style.inkMuted
                    }
                }
            }
        }

        UI.IconButton {
            visible: !card.fromMail && !card.task.done
            enabled: !card.task.readOnly
            iconName: "star"
            iconColor: card.task.important ? MailIcons.star : UI.Style.iconMuted
            tip: card.task.important ? qsTr("Not important") : qsTr("Important")
            onClicked: card.starred()
        }
    }
}
