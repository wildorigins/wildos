import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// System > History (SettingsHistory.dc): what happened on this PC, in plain
// words, newest first and grouped by day. The records are settings-history's
// (History), the words HistoryWords'. Details opens HistoryDetailDialog
// (HistoryDetail.dc). Kept 30 days; nothing is sent anywhere.
PageScroll {
    id: page

    property string filter: "all"
    property string period: "week"
    property int limit: 120
    property date now: new Date()

    readonly property var periodStart: period === "today" ? new Date(now.getFullYear(), now.getMonth(), now.getDate())
        : new Date(now.getTime() - (period === "week" ? 7 : 30) * 86400000)
    readonly property string periodWords: period === "today" ? "today" : period === "week" ? "this week" : "in the last 30 days"
    readonly property string periodSpan: period === "today" ? "today" : period === "week" ? "in the last 7 days" : "in the last 30 days"

    // Every event in the period, with its words.
    readonly property var inPeriod: History.events.filter(e => e.when >= periodStart).map(e => ({ event: e, words: HistoryWords.describe(e, History.events) }))
    readonly property int problems: inPeriod.filter(x => x.words.problem).length
    // "camera" is the camera and microphone lights' This week link, not one
    // of the filters on the page.
    readonly property var shown: unreadable ? [] : inPeriod.filter(x => filter === "all" ? true : filter === "problems" ? x.words.problem
        : filter === "camera" ? x.event.kind === "device-use" : x.words.group === filter)
    readonly property bool unreadable: History.problem === "unreadable"

    // [{ label, rows: [{event, words}] }], newest day first.
    readonly property var days: {
        const out = [];
        const list = shown.slice(0, limit);
        for (let i = 0; i < list.length; ++i) {
            const day = Format.day(list[i].event.when);
            const label = day === "today" ? "Today" : day === "yesterday" ? "Yesterday" : day;
            if (out.length === 0 || out[out.length - 1].label !== label) {
                out.push({ label: label, rows: [] });
            }
            out[out.length - 1].rows.push(list[i]);
        }
        return out;
    }

    function emptyText() {
        if (page.filter === "problems") {
            return "No problems " + page.periodSpan + ".";
        }
        if (page.filter === "security") {
            return "Nothing to do with security " + page.periodSpan + ".";
        }
        if (page.filter === "updates") {
            return "No updates or restore points " + page.periodSpan + ".";
        }
        if (page.filter === "camera") {
            return "No app used the camera or microphone " + page.periodSpan + ".";
        }
        if (page.filter === "apps") {
            return "No apps installed, removed or closed unexpectedly " + page.periodSpan + ".";
        }
        return "Nothing has happened on this PC " + page.periodSpan + ".";
    }

    function filterName() {
        return ({ all: "Everything", problems: "Problems", security: "Security", updates: "Updates", apps: "Apps", camera: "Camera and microphone" })[page.filter];
    }

    function exportText() {
        const head = "Wild OS history: " + filterName() + ", " + page.periodSpan + "\n"
            + "Saved " + Format.longDate(new Date()) + " at " + Format.time(new Date()) + " from Settings > System > History\n";
        const body = page.shown.map(x => HistoryWords.fullText(x.event, History.events)).join("\n----------------------------------------\n\n");
        return head + "\n" + (body.length > 0 ? body : emptyText() + "\n");
    }

    // wildos-settings --page history-camera, from the lights' This week.
    function takeRequest() {
        if (Navigator.request === "camera-mic") {
            Navigator.request = "";
            page.filter = "camera";
            page.period = "week";
            page.limit = 120;
        }
    }

    Connections {
        target: Navigator
        function onRequestChanged() {
            page.takeRequest();
        }
    }

    Component.onCompleted: {
        takeRequest();
        History.refresh();
    }

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        // The summary: problems in the period, or that all went well.
        Rectangle {
            id: hero
            readonly property bool worry: page.unreadable || page.problems > 0
            Layout.fillWidth: true
            implicitHeight: heroRow.implicitHeight + 28
            radius: 12
            color: UI.Style.tone(0.05)
            border.width: 1
            border.color: UI.Style.tone(0.07)

            RowLayout {
                id: heroRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 18

                Rectangle {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    radius: 23
                    color: Qt.rgba(heroIcon.color.r, heroIcon.color.g, heroIcon.color.b, 0.16)

                    PathIcon {
                        id: heroIcon
                        anchors.centerIn: parent
                        name: hero.worry ? "triangle-alert" : "status-ok"
                        size: 24
                        strokeWidth: 2.1
                        color: hero.worry ? HistoryWords.amber : HistoryWords.green
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    UI.Label {
                        Layout.fillWidth: true
                        text: page.unreadable ? "History can't be shown"
                            : page.problems > 0 ? Format.plural(page.problems, "thing", "things") + " worth a look " + page.periodWords
                            : "Nothing needs a look " + page.periodWords
                        size: 16.5
                        font.weight: UI.Style.weightBold
                        wrapMode: Text.Wrap
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: page.unreadable ? History.problemDetail + " Restarting the PC usually fixes this."
                            : page.problems > 0 ? "Everything else went as it should. Choose Problems to see only those."
                            : "Everything went as it should."
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                UI.Button {
                    implicitHeight: 32
                    text: "Report a problem"
                    onClicked: {
                        if (!History.openReport()) {
                            notice.show("Report a problem isn't installed", "Install it from the Store to send a report.", true);
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Segmented {
                enabled: !page.unreadable
                options: [{ value: "all", text: "Everything" }, { value: "problems", text: "Problems" }, { value: "security", text: "Security" },
                    { value: "updates", text: "Updates" }, { value: "apps", text: "Apps" }]
                value: page.filter
                onPicked: v => {
                    page.filter = v;
                    page.limit = 120;
                }
            }

            Item {
                Layout.fillWidth: true
            }

            UI.ComboBox {
                Layout.preferredWidth: 150
                enabled: !page.unreadable
                Accessible.name: "Period"
                model: [{ value: "today", text: "Today" }, { value: "week", text: "Last 7 days" }, { value: "month", text: "Last 30 days" }]
                currentIndex: model.findIndex(o => o.value === page.period)
                onActivated: index => {
                    page.period = model[index].value;
                    page.limit = 120;
                }
            }
        }

        // The latest events could not be gathered, but older ones are here.
        RowLayout {
            Layout.fillWidth: true
            visible: History.problem === "stale"
            spacing: 8

            PathIcon {
                name: "triangle-alert"
                size: 14
                strokeWidth: 2
                color: HistoryWords.amber
            }

            UI.Label {
                Layout.fillWidth: true
                text: "Some of the latest events may be missing. " + History.problemDetail
                size: 12.5
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }

        Repeater {
            model: page.days

            ColumnLayout {
                id: dayBlock
                required property var modelData
                Layout.fillWidth: true
                spacing: 14

                UI.Label {
                    Layout.fillWidth: true
                    text: dayBlock.modelData.label.toUpperCase()
                    size: 12.5
                    font.weight: UI.Style.weightBold
                    font.letterSpacing: 0.25
                    color: UI.Style.inkSubtle
                    Accessible.role: Accessible.Heading
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: rowsColumn.implicitHeight
                    radius: 12
                    color: UI.Style.tone(0.05)
                    border.width: 1
                    border.color: UI.Style.tone(0.07)
                    clip: true

                    Column {
                        id: rowsColumn
                        width: parent.width

                        Repeater {
                            model: dayBlock.modelData.rows

                            T.AbstractButton {
                                id: eventRow
                                required property var modelData
                                required property int index
                                width: rowsColumn.width
                                height: 54
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.words.title + ". " + modelData.words.detail
                                Accessible.description: "Details"
                                onClicked: details.openFor(modelData.event)

                                background: Rectangle {
                                    color: eventRow.hovered ? UI.Style.tone(0.03) : "transparent"
                                    radius: 12

                                    Rectangle {
                                        visible: eventRow.index > 0
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        height: 1
                                        color: UI.Style.tone(0.07)
                                    }

                                    UI.FocusRing {
                                        visible: eventRow.visualFocus
                                        targetRadius: 12
                                    }
                                }

                                contentItem: RowLayout {
                                    spacing: 14

                                    Item {
                                        Layout.leftMargin: 18
                                        // The board's 38px fits a 24-hour clock; "10:42 PM" needs more.
                                        Layout.preferredWidth: Format.twelveHour() ? 60 : 38
                                        Layout.fillHeight: true

                                        UI.Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: Format.time(eventRow.modelData.event.when)
                                            size: 12.5
                                            color: UI.Style.inkMuted
                                            font.features: ({ "tnum": 1 })
                                        }
                                    }

                                    PathIcon {
                                        name: eventRow.modelData.words.icon
                                        size: 19
                                        strokeWidth: 1.9
                                        color: eventRow.modelData.words.tone
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        UI.Label {
                                            Layout.fillWidth: true
                                            text: eventRow.modelData.words.title
                                            size: 13.5
                                            font.weight: UI.Style.weightMedium
                                        }

                                        UI.Label {
                                            Layout.fillWidth: true
                                            text: eventRow.modelData.words.detail
                                            size: 12.5
                                            color: UI.Style.inkMuted
                                        }
                                    }

                                    UI.LinkButton {
                                        Layout.rightMargin: 18
                                        text: "Details"
                                        Accessible.name: "Details of " + eventRow.modelData.words.title
                                        onClicked: details.openFor(eventRow.modelData.event)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Nothing in the period, or nothing for the filter chosen.
        Rectangle {
            Layout.fillWidth: true
            visible: page.shown.length === 0 && !page.unreadable
            implicitHeight: 54
            radius: 12
            color: UI.Style.tone(0.05)
            border.width: 1
            border.color: UI.Style.tone(0.07)

            UI.Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: History.loading && History.events.length === 0 ? "Looking at what happened…" : page.emptyText()
                size: 13
                color: UI.Style.inkMuted
            }
        }

        UI.LinkButton {
            visible: page.shown.length > page.limit
            text: "Show " + Math.min(120, page.shown.length - page.limit) + " more"
            onClicked: page.limit += 120
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            PathIcon {
                name: "lock-round"
                size: 14
                strokeWidth: 2
                color: UI.Style.inkMuted
            }

            UI.Label {
                Layout.fillWidth: true
                text: "Kept on this PC for 30 days, then deleted. Nothing is sent anywhere."
                size: 12.5
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            UI.LinkButton {
                text: "Save as a file"
                visible: !page.unreadable
                onClicked: {
                    saveDialog.selectedFile = History.defaultSaveFile();
                    saveDialog.open();
                }
            }
        }
    }

    Timer {
        interval: 60000
        running: page.visible
        repeat: true
        onTriggered: {
            page.now = new Date();
            History.refresh();
        }
    }

    HistoryDetailDialog {
        id: details
        onFailed: message => notice.show("Report a problem couldn't open", message, true)
    }

    FileDialog {
        id: saveDialog
        title: "Save history as a file"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: ["Text files (*.txt)"]
        onAccepted: {
            if (History.saveTo(selectedFile, page.exportText())) {
                const parts = decodeURIComponent(selectedFile.toString()).split("/");
                notice.show("History saved", Format.plural(page.shown.length, "event was", "events were") + " saved as " + parts[parts.length - 1]
                    + ", in " + parts[parts.length - 2] + ".", false);
            } else {
                notice.show("History wasn't saved", "That folder can't be written to. Choose another one.", true);
            }
        }
    }
}
