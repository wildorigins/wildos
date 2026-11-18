pragma Singleton

import QtQuick
import WildOS.UI as UI

// Times and counts in the house style: day-first dates, plain words for
// recent times, and times in the regional format on the clock chosen on
// the Date and time page.
QtObject {
    function pad(n) {
        return n < 10 ? "0" + n : "" + n;
    }

    function time(date) {
        return Qt.locale().toString(date, UI.TimeFormat.shortFormat);
    }

    // Whether times carry AM or PM, from the clock chosen or the regional
    // format's own.
    function twelveHour() {
        return /a/i.test(UI.TimeFormat.shortFormat.replace(/'[^']*'/g, ""));
    }

    readonly property var days: [qsTr("Sunday"), qsTr("Monday"), qsTr("Tuesday"), qsTr("Wednesday"), qsTr("Thursday"),
        qsTr("Friday"), qsTr("Saturday")]
    readonly property var months: [qsTr("January"), qsTr("February"), qsTr("March"), qsTr("April"), qsTr("May"),
        qsTr("June"), qsTr("July"), qsTr("August"), qsTr("September"), qsTr("October"), qsTr("November"),
        qsTr("December")]

    // "Tuesday 21 October"
    function longDate(date) {
        return qsTr("%1 %2 %3").arg(days[date.getDay()]).arg(date.getDate()).arg(months[date.getMonth()]);
    }

    function shortDate(date) {
        return pad(date.getDate()) + "/" + pad(date.getMonth() + 1) + "/" + date.getFullYear();
    }

    function plural(n, one, many) {
        return n === 1 ? "1 " + one : n + " " + many;
    }

    // "today", "yesterday", "Tuesday" within the week, then "21 October".
    // "today" and "yesterday" stay in English because callers compare
    // them; dayText() and dayTitle() are the words to show.
    function day(date) {
        if (!isValid(date)) {
            return "";
        }
        const now = new Date();
        const midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        const then = new Date(date.getFullYear(), date.getMonth(), date.getDate());
        const diff = Math.round((midnight - then) / 86400000);
        if (diff <= 0) {
            return "today";
        }
        if (diff === 1) {
            return "yesterday";
        }
        if (diff < 7) {
            return days[date.getDay()];
        }
        return date.getFullYear() === now.getFullYear() ? qsTr("%1 %2").arg(date.getDate()).arg(months[date.getMonth()])
            : qsTr("%1 %2 %3").arg(date.getDate()).arg(months[date.getMonth()]).arg(date.getFullYear());
    }

    // day() for the middle of a sentence: "today", "yesterday", "Tuesday".
    function dayText(date) {
        const d = day(date);
        return d === "today" ? qsTr("today") : d === "yesterday" ? qsTr("yesterday") : d;
    }

    // day() at the start of a line: "Today", "Yesterday", "Tuesday".
    function dayTitle(date) {
        const d = day(date);
        return d === "today" ? qsTr("Today") : d === "yesterday" ? qsTr("Yesterday") : d.charAt(0).toUpperCase() + d.slice(1);
    }

    // "today at 13:10", "yesterday at 13:10", "on Tuesday at 13:10".
    function dayAndTime(date) {
        const d = day(date);
        return d === "today" ? qsTr("today at %1").arg(time(date))
            : d === "yesterday" ? qsTr("yesterday at %1").arg(time(date))
            : qsTr("on %1 at %2").arg(d).arg(time(date));
    }

    function isValid(date) {
        return date instanceof Date && !isNaN(date.getTime());
    }

    // "just now", "10 minutes ago", "today at 13:10", "06/11/2026 at 13:10"
    function ago(date) {
        if (!isValid(date)) {
            return "";
        }
        const now = new Date();
        const minutes = Math.floor((now - date) / 60000);
        if (minutes < 1) {
            return qsTr("just now");
        }
        if (minutes < 60) {
            return minutes === 1 ? qsTr("1 minute ago") : qsTr("%1 minutes ago").arg(minutes);
        }
        const sameDay = now.toDateString() === date.toDateString();
        if (sameDay) {
            return qsTr("today at %1").arg(time(date));
        }
        const yesterday = new Date(now.getTime() - 86400000);
        if (yesterday.toDateString() === date.toDateString()) {
            return qsTr("yesterday at %1").arg(time(date));
        }
        return qsTr("%1 at %2").arg(shortDate(date)).arg(time(date));
    }
}
