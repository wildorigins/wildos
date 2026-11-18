import QtCore
import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// An invitation in the reading pane (MailMsInvite.dc): Accept, Maybe and
// Decline, when and where, who's coming, and the day it's on with whatever
// it overlaps. The answer goes through Calendar: where a Google or CalDAV
// calendar already has the event, its server tells the organiser;
// otherwise it goes into your calendar and the organiser gets a reply by
// mail. A cancellation offers to take the event out.
Rectangle {
    id: card

    // The iCalendar text in the message, and the account it came to.
    property string ics
    property string accountId
    property string myEmail
    property string messageKey

    readonly property var invite: ics.length > 0 ? CalendarEngine.describeInvitation(ics, myEmail) : ({})
    readonly property bool isRequest: invite.method === "request"
    readonly property bool isCancel: invite.method === "cancel"
    readonly property string answerKey: "invites/" + (invite.uid || "").replace(/[\/\\]/g, "_") + "_" + (invite.sequence || 0)
    property string answered: ""
    property var dayEvents: []
    property bool busy: false

    visible: invite.uid !== undefined && (isRequest || isCancel)
    implicitHeight: visible ? content.implicitHeight + 32 : 0
    radius: UI.Style.radiusSm
    color: UI.Style.surfaceRaised
    border.width: 1
    border.color: UI.Style.borderSubtle

    onInviteChanged: {
        answered = invite.uid ? (store.value(answerKey, "") || invite.myStatus || "") : "";
        if (answered === "waiting") {
            answered = "";
        }
        dayEvents = [];
        if (invite.start && !invite.allDay) {
            CalendarEngine.loadDay(new Date(invite.start));
        }
    }

    function answer(status) {
        busy = true;
        CalendarEngine.answerInvitation(ics, status, myEmail);
    }
    function whenText() {
        const s = new Date(invite.start);
        const e = new Date(invite.end);
        const day = Formats.dayAndShortDate(s);
        if (invite.allDay) {
            return e > s ? qsTr("%1 to %2").arg(day).arg(Formats.dayAndShortDate(e)) : qsTr("%1, all day").arg(day);
        }
        return qsTr("%1, %2 to %3").arg(day).arg(Formats.time(s)).arg(Formats.time(e));
    }
    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }

    Settings {
        id: store
        category: "mail"
    }

    // The reply by mail, for an organiser no calendar server will tell.
    Composer {
        id: replyComposer
    }

    Connections {
        target: CalendarEngine
        function onDayLoaded(day, events) {
            if (card.invite.start && card.sameDay(day, new Date(card.invite.start))) {
                card.dayEvents = events.filter(e => !e.allDay && e.title !== card.invite.title);
            }
        }
        function onInvitationAnswered(uid, status, reply, organizer, title) {
            if (uid !== card.invite.uid) {
                return;
            }
            card.busy = false;
            card.answered = status;
            store.setValue(card.answerKey, status);
            if (reply.length > 0 && organizer.length > 0) {
                const verb = status === "going" ? qsTr("Accepted") : status === "maybe" ? qsTr("Tentative") : qsTr("Declined");
                replyComposer.start({ accountId: card.accountId, to: [organizer], subject: verb + ": " + title, calendarReply: reply });
                const words = status === "going" ? qsTr("I'll be there.") : status === "maybe" ? qsTr("I might be able to make it.") : qsTr("I can't make it, sorry.");
                replyComposer.bodyText = words;
                replyComposer.bodyHtml = "<p>" + words + "</p>";
                replyComposer.send();
            }
        }
        function onNotice(text) {
            card.busy = false;
        }
    }

    RowLayout {
        id: content
        x: 16
        y: 16
        width: card.width - 32
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 12

            RowLayout {
                spacing: 8
                Rectangle {
                    implicitWidth: badge.implicitWidth + 14
                    implicitHeight: badge.implicitHeight + 6
                    radius: 4
                    color: card.isCancel ? Qt.alpha(UI.Style.danger, 0.18) : Qt.alpha(UI.Style.warning, 0.18)
                    UI.Label {
                        id: badge
                        anchors.centerIn: parent
                        text: card.isCancel ? qsTr("Cancelled") : qsTr("Invitation")
                        size: 11.5
                        font.weight: UI.Style.weightSemiBold
                        color: card.isCancel ? UI.Style.danger : UI.Style.warning
                    }
                }
                UI.Label {
                    visible: card.answered.length > 0 && card.isRequest
                    text: card.answered === "going" ? qsTr("You're going") : card.answered === "maybe" ? qsTr("You said maybe") : qsTr("You declined")
                    size: 12
                    color: UI.Style.inkMuted
                }
            }

            // Your answer.
            RowLayout {
                visible: card.isRequest
                spacing: 8
                Repeater {
                    model: [
                        { id: "going", text: qsTr("Accept"), icon: "check" },
                        { id: "maybe", text: qsTr("Maybe"), icon: "more" },
                        { id: "notGoing", text: qsTr("Decline"), icon: "close" }
                    ]
                    delegate: UI.Button {
                        required property var modelData
                        size: "small"
                        kind: card.answered === modelData.id || (card.answered === "" && modelData.id === "going") ? "primary" : "secondary"
                        text: modelData.text
                        iconName: modelData.icon
                        enabled: !card.busy
                        onClicked: card.answer(modelData.id)
                    }
                }
            }
            UI.Button {
                visible: card.isCancel
                size: "small"
                text: qsTr("Remove from calendar")
                iconName: "delete"
                onClicked: CalendarEngine.removeInvitation(card.ics)
            }

            component Line: RowLayout {
                id: line
                property string icon
                property alias text: main.text
                property alias subtext: sub.text
                default property alias extra: extraRow.data
                Layout.fillWidth: true
                spacing: 12
                MailIcon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: line.icon
                    size: 16
                    color: UI.Style.iconMuted
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    UI.Label {
                        id: main
                        Layout.fillWidth: true
                        size: 13.5
                        wrapMode: Text.Wrap
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

            Line {
                icon: "clock"
                text: card.invite.start ? card.whenText() : ""
                subtext: card.invite.recurring ? qsTr("Repeats") : qsTr("Doesn't repeat")
            }
            Line {
                visible: (card.invite.location || "").length > 0
                icon: "location"
                text: card.invite.location || ""
                UI.LinkButton {
                    text: qsTr("Open in Maps")
                    size: 12
                    onClicked: Qt.openUrlExternally("https://www.openstreetmap.org/search?query=" + encodeURIComponent(card.invite.location))
                }
            }
            Line {
                visible: card.invite.people > 0
                icon: "people"
                text: {
                    const who = card.invite.organizerName || card.invite.organizer || "";
                    const others = card.invite.people - 1;
                    return others > 0 ? (others === 1 ? qsTr("%1 and 1 other").arg(who) : qsTr("%1 and %2 others").arg(who).arg(others)) : who;
                }
                subtext: {
                    const parts = [];
                    if (card.invite.accepted > 0)
                        parts.push(qsTr("%n accepted", "", card.invite.accepted));
                    if (card.invite.waiting > 0)
                        parts.push(qsTr("%n not replied yet", "", card.invite.waiting));
                    return parts.join(", ");
                }
            }
        }

        // The day it's on (MailMsInvite.dc, right).
        Rectangle {
            id: dayBox
            visible: !card.invite.allDay && card.invite.start !== undefined && card.width > 620
            Layout.preferredWidth: 300
            Layout.preferredHeight: 280
            Layout.alignment: Qt.AlignTop
            radius: UI.Style.radiusSm
            color: UI.Style.surfaceRest
            border.width: 1
            border.color: UI.Style.borderSubtle
            clip: true

            readonly property date start: card.invite.start ? new Date(card.invite.start) : new Date()
            readonly property date end: card.invite.end ? new Date(card.invite.end) : new Date()
            readonly property real startHour: start.getHours() + start.getMinutes() / 60
            readonly property real endHour: Math.max(startHour + 0.5, end.getHours() + end.getMinutes() / 60 + (end.getDate() !== start.getDate() ? 24 : 0))
            // From an hour before to an hour after, as the board shows it.
            readonly property int firstHour: Math.max(0, Math.floor(startHour) - 2)
            readonly property real hour: 34
            readonly property var clashes: card.dayEvents.filter(e => e.busy && e.startMinute < endHour * 60 && e.endMinute > startHour * 60)

            UI.Label {
                x: 12
                y: 10
                text: Formats.weekdayAndDay(dayBox.start)
                size: 13
                font.weight: UI.Style.weightSemiBold
            }
            Rectangle {
                x: 12
                y: 34
                width: parent.width - 24
                height: 28
                radius: UI.Style.radiusXs
                color: dayBox.clashes.length > 0 ? UI.Style.warningSoft : UI.Style.successSoft
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    MailIcon {
                        name: dayBox.clashes.length > 0 ? "clock" : "check"
                        size: 13
                        color: dayBox.clashes.length > 0 ? UI.Style.warning : UI.Style.success
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: dayBox.clashes.length > 0 ? qsTr("Overlaps %1").arg(dayBox.clashes[0].title) : qsTr("Nothing else on at this time")
                        size: 12
                        elide: Text.ElideRight
                    }
                }
            }
            Item {
                id: hours
                x: 12
                y: 70
                width: parent.width - 24
                height: parent.height - 76
                clip: true
                // Side by side when they overlap: what's on at the left,
                // the invitation at the right.
                readonly property real lane: dayBox.clashes.length > 0 ? (width - 52) / 2 : width - 52
                Repeater {
                    model: 7
                    delegate: Item {
                        required property int index
                        y: 8 + index * dayBox.hour
                        width: parent.width
                        height: dayBox.hour
                        UI.Label {
                            y: -6
                            text: Formats.hour((dayBox.firstHour + index) % 24)
                            size: 11
                            color: UI.Style.inkSubtle
                        }
                        Rectangle {
                            x: 46
                            width: parent.width - 46
                            height: 1
                            color: UI.Style.borderSubtle
                        }
                    }
                }
                Repeater {
                    model: card.dayEvents
                    delegate: Rectangle {
                        required property var modelData
                        x: 50
                        y: 8 + (modelData.startMinute / 60 - dayBox.firstHour) * dayBox.hour + 1
                        width: hours.lane - 2
                        height: Math.max(16, (modelData.endMinute - modelData.startMinute) / 60 * dayBox.hour - 2)
                        radius: 4
                        color: Qt.rgba(Qt.color(modelData.color).r, Qt.color(modelData.color).g, Qt.color(modelData.color).b, 0.4)
                        Rectangle {
                            width: 3
                            height: parent.height
                            color: modelData.color
                        }
                        UI.Label {
                            x: 8
                            y: 2
                            width: parent.width - 12
                            text: modelData.title
                            size: 11.5
                            font.weight: UI.Style.weightSemiBold
                            elide: Text.ElideRight
                        }
                    }
                }
                // The invitation, dashed.
                Rectangle {
                    x: 50 + (dayBox.clashes.length > 0 ? hours.lane : 0)
                    y: 8 + (dayBox.startHour - dayBox.firstHour) * dayBox.hour + 1
                    width: hours.lane
                    height: Math.max(16, (dayBox.endHour - dayBox.startHour) * dayBox.hour - 2)
                    radius: 4
                    color: UI.Style.accentSoft
                    border.width: 1.5
                    border.color: UI.Style.accent
                    UI.Label {
                        x: 8
                        y: 2
                        width: parent.width - 12
                        text: card.invite.title || ""
                        size: 11.5
                        font.weight: UI.Style.weightSemiBold
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
