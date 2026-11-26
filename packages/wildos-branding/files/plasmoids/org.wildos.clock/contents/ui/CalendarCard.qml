pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI
import "Formats.js" as Formats

// The design's calendar card: today's date as the heading, the month shown
// beneath it with previous and next buttons, a month grid that starts on the
// regional format's first day of the week with today in the accent, and a
// line along the foot about the day chosen. The month's name opens the
// months of the year, and the year opens twelve years, as the Windows
// calendar zooms out from its title; the wheel and the arrows step whichever
// is showing.
// Wild OS has no calendar of events, so choosing a day says how far away
// it is, the question a calendar flyout mostly answers, rather than
// claiming the day is free.
ColumnLayout {
    id: card

    required property date today

    // The clock may tick every second; the grid only needs the day.
    readonly property int todayStamp: today.getFullYear() * 10000 + today.getMonth() * 100 + today.getDate()
    readonly property var todayDate: new Date(Math.floor(todayStamp / 10000), Math.floor(todayStamp / 100) % 100, todayStamp % 100)

    property int year: today.getFullYear()
    property int month: today.getMonth()
    // "days" of a month, "months" of a year, or twelve "years".
    property string mode: "days"
    // The first of the twelve years shown.
    property int yearStart: 0
    readonly property bool showingToday: mode === "days" && year === todayDate.getFullYear() && month === todayDate.getMonth()
    // The day picked in the grid; null means today.
    property var chosen: null
    // Where the keyboard is in the grid: a day, or a month or year cell.
    property var keyDate: null
    property int keyCell: 0
    readonly property var keyDay: {
        for (const day of [keyDate, chosen, todayDate]) {
            if (day !== null && day.getFullYear() === year && day.getMonth() === month) {
                return day;
            }
        }
        return new Date(year, month, 1);
    }

    property alias cells: cells

    // Windows' calendar starts in 1601; JavaScript reads a year under 100
    // as one in the 1900s.
    readonly property int firstYear: 1601
    readonly property int lastYear: 9999

    function stops(): var {
        const list = mode === "years" ? [] : [title];
        if (!showingToday) {
            list.push(todayButton);
        }
        return list.concat([previous, next, cells]);
    }

    readonly property var locale: Formats.names()
    readonly property int firstDay: Formats.firstDayOfWeek()

    // Six weeks from the first day of the week on or before the 1st, so the
    // card keeps one height from month to month.
    readonly property var days: {
        const first = new Date(year, month, 1);
        const offset = (first.getDay() - firstDay + 7) % 7;
        const list = [];
        for (let i = 0; i < 42; i++) {
            const day = new Date(year, month, 1 - offset + i);
            list.push({
                date: day,
                n: day.getDate(),
                inMonth: day.getMonth() === month,
                isToday: sameDay(day, todayDate),
                label: Formats.longDate(day)
            });
        }
        return list;
    }

    // The twelve months of the year shown, or twelve years.
    readonly property var pickerCells: {
        const list = [];
        for (let i = 0; i < 12; i++) {
            if (mode === "years") {
                const y = yearStart + i;
                list.push({ text: String(y), label: String(y), now: y === todayDate.getFullYear(), shown: y === year,
                            allowed: y >= firstYear && y <= lastYear });
            } else {
                list.push({ text: locale.standaloneMonthName(i, Locale.ShortFormat),
                            label: locale.toString(new Date(year, i, 1), "MMMM yyyy"),
                            now: i === todayDate.getMonth() && year === todayDate.getFullYear(), shown: i === month, allowed: true });
            }
        }
        return list;
    }

    readonly property string titleText: mode === "days" ? locale.toString(new Date(year, month, 1), "MMMM yyyy")
        : mode === "months" ? locale.toString(new Date(year, 0, 1), "yyyy")
        : qsTr("%1 to %2").arg(yearStart).arg(yearStart + 11)

    // A month, a year or twelve years on, whichever is showing.
    function step(count: int): void {
        if (mode === "days") {
            const target = new Date(year, month + count, 1);
            if (target.getFullYear() >= firstYear && target.getFullYear() <= lastYear) {
                year = target.getFullYear();
                month = target.getMonth();
            }
        } else if (mode === "months") {
            year = Math.max(firstYear, Math.min(lastYear, year + count));
        } else if (yearStart + 12 * count + 11 >= firstYear && yearStart + 12 * count <= lastYear) {
            yearStart += 12 * count;
        }
    }

    function goToToday(): void {
        year = todayDate.getFullYear();
        month = todayDate.getMonth();
        chosen = null;
        keyDate = null;
        mode = "days";
    }

    function zoomOut(): void {
        if (mode === "days") {
            keyCell = month;
            mode = "months";
        } else if (mode === "months") {
            yearStart = year - year % 12;
            keyCell = year - yearStart;
            mode = "years";
        }
    }

    function pickCell(index: int): void {
        if (!pickerCells[index].allowed) {
            return;
        }
        if (mode === "years") {
            year = yearStart + index;
            keyCell = month;
            mode = "months";
        } else {
            month = index;
            keyDate = null;
            mode = "days";
        }
    }

    function pick(day: var): void {
        chosen = sameDay(day, todayDate) ? null : day;
        keyDate = day;
        year = day.getFullYear();
        month = day.getMonth();
    }

    // The arrow keys while the keyboard is on the grid: a day or a week
    // either way, running on into the next month, or a cell in the months
    // and years.
    function handleKey(key: int): bool {
        const across = mode === "days" ? 7 : 4;
        let move = 0;
        switch (key) {
        case Qt.Key_Left:
            move = -1;
            break;
        case Qt.Key_Right:
            move = 1;
            break;
        case Qt.Key_Up:
            move = -across;
            break;
        case Qt.Key_Down:
            move = across;
            break;
        default:
            return false;
        }
        if (mode !== "days") {
            keyCell = Math.max(0, Math.min(11, keyCell + move));
            return true;
        }
        const target = new Date(keyDay.getFullYear(), keyDay.getMonth(), keyDay.getDate() + move);
        if (target.getFullYear() >= firstYear && target.getFullYear() <= lastYear) {
            keyDate = target;
            year = target.getFullYear();
            month = target.getMonth();
        }
        return true;
    }

    function sameDay(a: var, b: var): bool {
        return a !== null && b !== null && a.getFullYear() === b.getFullYear()
            && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }

    // ISO weeks, as Windows numbers them in the UK.
    function weekNumber(day: var): int {
        const d = new Date(Date.UTC(day.getFullYear(), day.getMonth(), day.getDate()));
        const weekday = (d.getUTCDay() + 6) % 7;
        d.setUTCDate(d.getUTCDate() - weekday + 3);
        const firstThursday = new Date(Date.UTC(d.getUTCFullYear(), 0, 4));
        return 1 + Math.round(((d - firstThursday) / 86400000 - 3 + (firstThursday.getUTCDay() + 6) % 7) / 7);
    }

    readonly property string footer: {
        const day = chosen !== null ? chosen : todayDate;
        const start = todayDate;
        const days = Math.round((new Date(day.getFullYear(), day.getMonth(), day.getDate()) - start) / 86400000);
        const when = days === 0 ? qsTr("Today") : days === 1 ? qsTr("Tomorrow") : days === -1 ? qsTr("Yesterday")
            : days > 0 ? qsTr("In %1 days").arg(days) : qsTr("%1 days ago").arg(days * -1);
        if (days === 0) {
            return qsTr("Today, week %1").arg(weekNumber(day));
        }
        return qsTr("%1, %2").arg(Formats.dayAndMonth(day)).arg(when.charAt(0).toLowerCase() + when.slice(1));
    }

    spacing: 12

    readonly property int headRow: 26
    readonly property int dayRow: 36
    readonly property int rowGap: 2

    // Scrolling up goes back and down goes on, a notch at a time; a
    // touchpad's small movements add up to notches.
    WheelHandler {
        property real travelled: 0
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            travelled += event.angleDelta.y;
            const notches = travelled > 0 ? Math.floor(travelled / 120) : Math.ceil(travelled / 120);
            if (notches !== 0) {
                travelled -= notches * 120;
                card.step(-notches);
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            UI.Label {
                Layout.fillWidth: true
                text: Formats.dayAndMonth(card.today)
                size: 14
                font.weight: UI.Style.weightSemiBold
                Accessible.role: Accessible.Heading
            }

            CardButton {
                id: title
                Layout.leftMargin: -6
                Layout.preferredWidth: titleLabel.implicitWidth + 12
                Layout.preferredHeight: titleLabel.implicitHeight
                fillHeight: height + 6
                radius: 6
                enabled: card.mode !== "years"
                accessibleName: card.mode === "days" ? qsTr("%1, choose a month").arg(card.titleText)
                    : card.mode === "months" ? qsTr("%1, choose a year").arg(card.titleText)
                    : card.titleText
                onClicked: card.zoomOut()

                UI.Label {
                    id: titleLabel
                    anchors.centerIn: parent
                    text: card.titleText
                    size: 12
                    color: UI.Style.inkMuted
                }
            }
        }

        CardButton {
            id: todayButton
            visible: !card.showingToday
            Layout.preferredWidth: todayLabel.implicitWidth + 20
            Layout.preferredHeight: 44
            fillHeight: 32
            radius: 8
            accessibleName: qsTr("Back to this month")
            onClicked: card.goToToday()

            UI.Label {
                id: todayLabel
                anchors.centerIn: parent
                text: qsTr("Today")
                size: 12
                color: UI.Style.accentText
                font.underline: todayButton.hovered
            }
        }

        RowLayout {
            spacing: 2

            CardButton {
                id: previous
                Layout.preferredWidth: 32
                Layout.preferredHeight: 44
                fillHeight: 32
                accessibleName: card.mode === "days" ? qsTr("Previous month") : card.mode === "months" ? qsTr("Previous year") : qsTr("Earlier years")
                onClicked: card.step(-1)

                UI.Glyph {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    path: "M15 6l-6 6 6 6"
                    strokeWidth: 2.2
                }
            }

            CardButton {
                id: next
                Layout.preferredWidth: 32
                Layout.preferredHeight: 44
                fillHeight: 32
                accessibleName: card.mode === "days" ? qsTr("Next month") : card.mode === "months" ? qsTr("Next year") : qsTr("Later years")
                onClicked: card.step(1)

                UI.Glyph {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    path: "M9 6l6 6-6 6"
                    strokeWidth: 2.2
                }
            }
        }
    }

    // The keyboard's stop for the whole grid: the arrows move inside it and
    // Enter picks what they are on.
    Item {
        id: cells
        Layout.fillWidth: true
        // The buttons above are 44 px for touch; the design's gap is to their
        // 32 px fill.
        Layout.topMargin: -6
        // The height of the month's six weeks, whichever grid is showing.
        implicitHeight: card.headRow + 6 * (card.dayRow + card.rowGap)

        readonly property bool keyboardInside: Nav.keyboard && Nav.current === cells

        function activate(): void {
            if (card.mode === "days") {
                card.pick(card.keyDay);
            } else {
                card.pickCell(card.keyCell);
            }
        }

        GridLayout {
            id: grid
            anchors.fill: parent
            visible: card.mode === "days"
            // The next month rises gently into place and the previous one drops
            // in from above, as the Windows calendar does.
            transform: UI.Reveal {
                item: grid
                key: card.year * 12 + card.month
                directional: true
                dy: UI.Style.travelSmall
                duration: UI.Style.motionFast
            }
            columns: 7
            columnSpacing: 2
            rowSpacing: card.rowGap
            Accessible.role: Accessible.Table
            Accessible.name: card.locale.toString(new Date(card.year, card.month, 1), "MMMM yyyy")

            Repeater {
                model: [qsTr("Su"), qsTr("Mo"), qsTr("Tu"), qsTr("We"), qsTr("Th"), qsTr("Fr"), qsTr("Sa")].map((_, i, names) => names[(i + card.firstDay) % 7])

                UI.Label {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: card.headRow
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData
                    size: 11
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkMuted
                }
            }

            Repeater {
                model: card.days

                MouseArea {
                    id: cell
                    required property var modelData
                    readonly property bool picked: card.sameDay(card.chosen, modelData.date) && !modelData.isToday
                    readonly property bool keyHere: cells.keyboardInside && card.sameDay(card.keyDay, modelData.date)
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: card.dayRow
                    z: keyHere ? 1 : 0
                    hoverEnabled: true
                    Accessible.role: Accessible.Cell
                    Accessible.name: modelData.isToday ? qsTr("%1, today").arg(modelData.label) : modelData.label
                    Accessible.selected: picked
                    Accessible.focused: keyHere
                    onClicked: card.pick(modelData.date)

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: card.dayRow
                        radius: height / 2
                        // Fades in on hover and clears at once, so moving along a list
                        // never lights two items. It comes before the colour so it sees
                        // the change in hover first.
                        Behavior on color {
                            enabled: cell.containsMouse
                            ColorAnimation { duration: UI.Style.hoverDuration }
                        }
                        color: cell.modelData.isToday ? (cell.pressed ? UI.Style.accentPressed : cell.containsMouse ? UI.Style.accentHover : UI.Style.accent)
                            : cell.pressed ? UI.Style.surfacePressed
                            : cell.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
                        border.width: cell.picked ? 2 : 0
                        border.color: UI.Style.accent
                    }

                    UI.Label {
                        anchors.fill: parent
                        horizontalAlignment: Text.AlignHCenter
                        text: cell.modelData.n
                        font.features: { "tnum": 1 }
                        font.weight: cell.modelData.isToday ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        color: cell.modelData.isToday ? UI.Style.textOnAccent
                            : cell.modelData.inMonth ? UI.Style.ink : UI.Style.inkSubtle
                    }

                    UI.FocusRing {
                        visible: cell.keyHere
                        targetRadius: card.dayRow / 2
                    }
                }
            }
        }

        GridLayout {
            id: picker
            anchors.fill: parent
            visible: card.mode !== "days"
            transform: UI.Reveal {
                item: picker
                key: card.mode === "months" ? card.year : card.yearStart
                directional: true
                dy: UI.Style.travelSmall
                duration: UI.Style.motionFast
            }
            columns: 4
            columnSpacing: 2
            rowSpacing: card.rowGap
            Accessible.role: Accessible.Table
            Accessible.name: card.titleText

            Repeater {
                model: card.pickerCells

                MouseArea {
                    id: choice
                    required property var modelData
                    required property int index
                    readonly property bool keyHere: cells.keyboardInside && card.keyCell === index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 1
                    z: keyHere ? 1 : 0
                    enabled: modelData.allowed
                    hoverEnabled: true
                    Accessible.role: Accessible.Cell
                    Accessible.name: modelData.label
                    Accessible.selected: modelData.shown
                    Accessible.focused: keyHere
                    onClicked: card.pickCell(index)

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: card.dayRow
                        radius: height / 2
                        // Fades in on hover and clears at once, so moving along a list
                        // never lights two items. It comes before the colour so it sees
                        // the change in hover first.
                        Behavior on color {
                            enabled: choice.containsMouse
                            ColorAnimation { duration: UI.Style.hoverDuration }
                        }
                        color: choice.modelData.now ? (choice.pressed ? UI.Style.accentPressed : choice.containsMouse ? UI.Style.accentHover : UI.Style.accent)
                            : choice.pressed ? UI.Style.surfacePressed
                            : choice.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
                        border.width: choice.modelData.shown && !choice.modelData.now ? 2 : 0
                        border.color: UI.Style.accent

                        UI.FocusRing {
                            visible: choice.keyHere
                            targetRadius: card.dayRow / 2
                        }
                    }

                    UI.Label {
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width - 8)
                        horizontalAlignment: Text.AlignHCenter
                        text: choice.modelData.text
                        font.features: { "tnum": 1 }
                        font.weight: choice.modelData.now ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        color: choice.modelData.now ? UI.Style.textOnAccent
                            : choice.modelData.allowed ? UI.Style.ink : UI.Style.inkDisabled
                    }
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.topMargin: 2
        implicitHeight: footerText.implicitHeight + 12

        Rectangle {
            width: parent.width
            height: 1
            color: UI.Style.tone(0.08)
        }

        UI.Label {
            id: footerText
            y: 12
            width: parent.width
            text: card.footer
            size: 12
            color: UI.Style.inkMuted
        }
    }
}
