import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// The card an event opens (CalMsEvent.dc): when and where, your answer if
// someone invited you, the reminder, the calendar, the notes and who's
// going, with Edit, Delete and Message everyone.
QQC.Popup {
    id: peek

    property var event: ({})
    property string calendarName
    readonly property bool has: event.key !== undefined

    signal editRequested(var event)
    signal messageEveryone(var addresses, string subject)

    function show(key, anchor) {
        const e = CalendarEngine.event(key);
        if (!e.key) {
            return;
        }
        event = e;
        const cal = CalendarEngine.calendars.find(c => c.uid === e.calendarUid);
        calendarName = cal ? cal.name : "";
        // Beside the event, on whichever side has room.
        const p = anchor.mapToItem(parent, 0, 0);
        const right = p.x + anchor.width + 12;
        x = right + width < parent.width - 8 ? right : Math.max(8, p.x - width - 12);
        y = Math.max(8, Math.min(parent.height - implicitHeight - 8, p.y - 40));
        open();
    }
    function dateText(e) {
        if (!e.start) {
            return "";
        }
        const s = new Date(e.start);
        const en = new Date(e.end);
        const day = Formats.dayAndShortDate(s);
        if (e.allDay) {
            const last = new Date(en.getFullYear(), en.getMonth(), en.getDate() - 1);
            return last > s ? qsTr("%1 to %2").arg(day).arg(Formats.dayAndShortDate(last)) : qsTr("%1, all day").arg(day);
        }
        return qsTr("%1, %2 to %3").arg(day).arg(Formats.time(s)).arg(Formats.time(en));
    }
    function reminderText(minutes) {
        if (minutes < 0) {
            return qsTr("No reminder");
        }
        if (minutes === 0) {
            return qsTr("Reminder when it starts");
        }
        if (minutes % 1440 === 0) {
            return minutes === 1440 ? qsTr("Reminder 1 day before") : minutes === 10080 ? qsTr("Reminder 1 week before") : qsTr("Reminder %1 days before").arg(minutes / 1440);
        }
        if (minutes % 60 === 0) {
            return minutes === 60 ? qsTr("Reminder 1 hour before") : qsTr("Reminder %1 hours before").arg(minutes / 60);
        }
        return minutes === 1 ? qsTr("Reminder 1 minute before") : qsTr("Reminder %1 minutes before").arg(minutes);
    }
    function statusText(s) {
        return s === "organiser" ? qsTr("Organiser") : s === "going" ? qsTr("Going") : s === "maybe" ? qsTr("Maybe")
             : s === "notGoing" ? qsTr("Not going") : qsTr("Not replied yet");
    }

    width: 384
    padding: 0
    modal: false
    focus: true
    closePolicy: QQC.Popup.CloseOnEscape | QQC.Popup.CloseOnPressOutside

    background: Rectangle {
        radius: 10
        color: UI.Style.glassMenu
        border.width: 1
        border.color: UI.Style.borderContextMenu
        Rectangle {
            width: parent.width
            height: 3
            topLeftRadius: 10
            topRightRadius: 10
            color: peek.event.color || UI.Style.accent
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            Layout.leftMargin: 16
            Layout.rightMargin: 10
            spacing: 2
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                UI.Label {
                    Layout.fillWidth: true
                    text: peek.event.title || ""
                    size: 18
                    font.weight: UI.Style.weightSemiBold
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
                UI.Label {
                    text: peek.dateText(peek.event) + (peek.event.recurring ? "  ·  " + qsTr("Repeats") : "")
                    size: 13
                    color: UI.Style.inkMuted
                }
            }
            UI.IconButton {
                Layout.alignment: Qt.AlignTop
                visible: !peek.event.readOnly
                iconName: "pencil"
                tip: qsTr("Edit")
                onClicked: {
                    peek.close();
                    peek.editRequested(peek.event);
                }
            }
            UI.IconButton {
                id: deleteButton
                Layout.alignment: Qt.AlignTop
                visible: !peek.event.readOnly
                iconName: "trash"
                tip: qsTr("Delete")
                onClicked: {
                    if (peek.event.recurring) {
                        deleteMenu.popup(deleteButton, 0, deleteButton.height + 4);
                    } else {
                        CalendarEngine.removeEvent(peek.event.key, true);
                        peek.close();
                    }
                }
            }
            UI.IconButton {
                Layout.alignment: Qt.AlignTop
                iconName: "close"
                tip: qsTr("Close")
                onClicked: peek.close()
            }
        }

        // Your answer, for an event someone else organised.
        RowLayout {
            visible: peek.event.invited === true
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 6
            Repeater {
                model: [
                    { id: "going", text: qsTr("Going"), icon: "check" },
                    { id: "maybe", text: qsTr("Maybe"), icon: "more" },
                    { id: "notGoing", text: qsTr("Not going"), icon: "close" }
                ]
                delegate: UI.Button {
                    required property var modelData
                    Layout.fillWidth: true
                    size: "small"
                    kind: peek.event.myStatus === modelData.id ? "primary" : "secondary"
                    text: modelData.text
                    iconName: modelData.icon
                    onClicked: {
                        CalendarEngine.respond(peek.event.key, modelData.id);
                        const e = peek.event;
                        e.myStatus = modelData.id;
                        peek.event = e;
                    }
                }
            }
        }

        component Detail: RowLayout {
            id: detail
            property string icon
            property alias text: label.text
            property alias subtext: sub.text
            property color dot: "transparent"
            default property alias extra: extraRow.data
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 12
            MailIcon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                name: detail.icon
                size: 16
                color: UI.Style.iconMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                RowLayout {
                    spacing: 8
                    Rectangle {
                        visible: detail.dot.a > 0
                        width: 9
                        height: 9
                        radius: 4.5
                        color: detail.dot
                    }
                    UI.Label {
                        id: label
                        Layout.fillWidth: true
                        size: 13
                        wrapMode: Text.Wrap
                    }
                }
                UI.Label {
                    id: sub
                    visible: text.length > 0
                    size: 12
                    color: UI.Style.inkMuted
                }
                Row {
                    id: extraRow
                }
            }
        }

        Detail {
            visible: (peek.event.location || "").length > 0
            icon: "location"
            text: peek.event.location || ""
            UI.LinkButton {
                text: qsTr("Open in Maps")
                onClicked: Qt.openUrlExternally("https://www.openstreetmap.org/search?query=" + encodeURIComponent(peek.event.location))
            }
        }
        Detail {
            icon: "bell"
            text: peek.reminderText(peek.event.reminder === undefined ? -1 : peek.event.reminder)
        }
        Detail {
            icon: "calendar"
            text: peek.calendarName
            dot: peek.event.color || "transparent"
        }
        Detail {
            visible: (peek.event.description || "").trim().length > 0
            icon: "notes"
            text: (peek.event.description || "").trim()
        }

        // Who's going.
        ColumnLayout {
            visible: (peek.event.people || []).length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                UI.Label {
                    Layout.fillWidth: true
                    text: (peek.event.people || []).length === 1 ? qsTr("1 person") : qsTr("%1 people").arg((peek.event.people || []).length)
                    size: 13
                    font.weight: UI.Style.weightSemiBold
                }
                UI.Label {
                    text: {
                        const parts = [];
                        if (peek.event.going > 0)
                            parts.push(qsTr("%n going", "", peek.event.going));
                        if (peek.event.maybe > 0)
                            parts.push(qsTr("%1 maybe").arg(peek.event.maybe));
                        return parts.join(", ");
                    }
                    size: 12
                    color: UI.Style.inkMuted
                }
            }
            Repeater {
                model: (peek.event.people || []).slice(0, 6)
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    SenderBadge {
                        size: 28
                        initials: Apps.initialsOf(modelData.name || modelData.email)
                        colourIndex: Apps.colourOf(modelData.email)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        UI.Label {
                            Layout.fillWidth: true
                            text: (modelData.name || modelData.email) + (modelData.me ? " " + qsTr("(you)") : "")
                            size: 13
                            elide: Text.ElideRight
                        }
                        UI.Label {
                            text: peek.statusText(modelData.status)
                            size: 12
                            color: modelData.status === "going" ? UI.Style.success : modelData.status === "maybe" ? UI.Style.warning
                                 : modelData.status === "notGoing" ? UI.Style.danger : UI.Style.inkMuted
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 16
            Layout.topMargin: 2
            spacing: 8
            UI.Button {
                visible: (peek.event.people || []).length > 0
                size: "small"
                text: qsTr("Message everyone")
                iconName: "mail"
                onClicked: {
                    const addresses = (peek.event.people || []).filter(p => !p.me).map(p => p.email);
                    peek.close();
                    peek.messageEveryone(addresses, peek.event.title);
                }
            }
            UI.Button {
                visible: !peek.event.readOnly
                size: "small"
                text: qsTr("Open event")
                onClicked: {
                    peek.close();
                    peek.editRequested(peek.event);
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }
    }

    UI.Menu {
        id: deleteMenu
        entries: [
            { text: qsTr("Delete this one"), run: () => { CalendarEngine.removeEvent(peek.event.key, false); peek.close(); } },
            { text: qsTr("Delete the whole series"), icon: "delete", danger: true, run: () => { CalendarEngine.removeEvent(peek.event.key, true); peek.close(); } }
        ]
    }
}
