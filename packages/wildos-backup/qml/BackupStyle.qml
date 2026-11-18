pragma Singleton

import QtQuick
import WildOS.UI as UI

// The values the board gives Backup that the system tokens do not have
// (AppBackup.dc, AppBackupLight.dc), and the words for sizes, times and
// days that its pages share.
QtObject {
    readonly property bool dark: UI.Style.dark
    readonly property bool contrast: UI.Style.contrast

    // The side column and the hairlines between the parts of the window.
    readonly property color sideShade: contrast ? "#000000" : Qt.rgba(0, 0, 0, dark ? 0.12 : 0.06)
    readonly property color hairline: contrast ? UI.Style.contrastDivider : dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.062)
    // Cards: 0.051 white on dark, 0.72 white on light, with the hairline.
    readonly property color card: UI.Style.surfaceRaised
    readonly property color field: contrast ? "#000000" : dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.72)
    readonly property color fieldEdge: contrast ? UI.Style.contrastEdge : dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.078)
    readonly property color track: contrast ? UI.Style.contrastDivider : dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.126)
    readonly property color rowHover: contrast ? UI.Style.surfaceHover : dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.049)
    readonly property color rowSelected: UI.Style.accentAlpha(0.16, 0.11)

    // Status only, in the system's own status colours: green when backed
    // up, amber when caught up late or waiting, red when a backup failed.
    readonly property color good: UI.Style.success
    readonly property color goodSoft: UI.Style.successSoft
    readonly property color late: UI.Style.warning
    readonly property color lateText: UI.Style.warning
    readonly property color lateSoft: UI.Style.warningSoft
    readonly property color bad: UI.Style.danger
    readonly property color badSoft: dark ? Qt.rgba(242 / 255, 139 / 255, 130 / 255, 0.14) : Qt.rgba(197 / 255, 55 / 255, 44 / 255, 0.10)
    readonly property color link: dark ? UI.Style.accent : UI.Style.accentText

    readonly property string icon: "qrc:/qt/qml/WildOS/Backup/data/wildos-backup.svg"

    // The Keep choices: what restic keeps, and how it reads.
    readonly property var keepWithin: [
        { value: "1d", text: qsTr("for a day") },
        { value: "2d", text: qsTr("for 2 days") },
        { value: "7d", text: qsTr("for a week") }
    ]
    readonly property var keepDaily: [
        { value: 7, text: qsTr("for a week") },
        { value: 14, text: qsTr("for 2 weeks") },
        { value: 31, text: qsTr("for a month") }
    ]
    readonly property var keepWeekly: [
        { value: 4, text: qsTr("for a month") },
        { value: 13, text: qsTr("for 3 months") },
        { value: 26, text: qsTr("for 6 months") },
        { value: 52, text: qsTr("for a year") }
    ]
    readonly property var keepMonthly: [
        { value: 6, text: qsTr("for 6 months") },
        { value: 12, text: qsTr("for a year") },
        { value: 24, text: qsTr("for 2 years") },
        { value: -1, text: qsTr("forever") }
    ]

    // "214 MB", "1.3 GB", "86 GB", in the decimal units drives are sold in.
    function size(bytes) {
        if (!(bytes >= 0)) {
            return "";
        }
        const units = [qsTr("bytes"), qsTr("KB"), qsTr("MB"), qsTr("GB"), qsTr("TB")];
        let v = bytes;
        let i = 0;
        while (v >= 1000 && i < units.length - 1) {
            v /= 1000;
            ++i;
        }
        if (i === 0) {
            return qsTr("%1 bytes").arg(Math.round(v));
        }
        const shown = v < 10 ? Math.round(v * 10) / 10 : Math.round(v);
        return Number(shown).toLocaleString(Qt.locale(), "f", shown % 1 === 0 ? 0 : 1) + " " + units[i];
    }

    function clock(date) {
        return Qt.formatTime(date, "HH:mm");
    }
    function dayStart(date) {
        return new Date(date.getFullYear(), date.getMonth(), date.getDate());
    }
    function daysAgo(date) {
        return Math.round((dayStart(new Date()) - dayStart(date)) / 86400000);
    }
    function dayName(date) {
        return Qt.locale().dayName(date.getDay(), Locale.LongFormat);
    }
    function shortDate(date) {
        return date.getFullYear() === new Date().getFullYear() ? Qt.formatDate(date, "d MMM") : Qt.formatDate(date, "d MMM yyyy");
    }

    // "Today, 09:12", "Yesterday, 09:00", "Saturday, 09:04", "2 Nov, 09:00".
    function when(date) {
        if (!date || isNaN(date)) {
            return "";
        }
        const ago = daysAgo(date);
        const day = ago === 0 ? qsTr("Today") : ago === 1 ? qsTr("Yesterday") : ago > 1 && ago < 7 ? dayName(date) : shortDate(date);
        return qsTr("%1, %2").arg(day).arg(clock(date));
    }
    // "today at 09:12", "yesterday at 09:00", "on Saturday at 09:04".
    function whenInSentence(date) {
        const ago = daysAgo(date);
        if (ago === 0) {
            return qsTr("today at %1").arg(clock(date));
        }
        if (ago === 1) {
            return qsTr("yesterday at %1").arg(clock(date));
        }
        return qsTr("on %1 at %2").arg(ago > 1 && ago < 7 ? dayName(date) : shortDate(date)).arg(clock(date));
    }
    // "Tomorrow at 09:00", "Today at 09:00", "Monday at 09:00".
    function next(date) {
        if (!date || isNaN(date)) {
            return "";
        }
        const ahead = -daysAgo(date);
        const day = ahead === 0 ? qsTr("Today") : ahead === 1 ? qsTr("Tomorrow") : ahead < 7 ? dayName(date) : shortDate(date);
        return qsTr("%1 at %2").arg(day).arg(clock(date));
    }
    // "less than a minute", "2 minutes", "1 hour 5 minutes".
    function duration(seconds) {
        const minutes = Math.round(seconds / 60);
        if (minutes < 1) {
            return qsTr("less than a minute");
        }
        if (minutes < 60) {
            return minutes === 1 ? qsTr("1 minute") : qsTr("%1 minutes").arg(minutes);
        }
        const h = Math.floor(minutes / 60);
        const m = minutes % 60;
        const hours = h === 1 ? qsTr("1 hour") : qsTr("%1 hours").arg(h);
        return m === 0 ? hours : m === 1 ? qsTr("%1 1 minute").arg(hours) : qsTr("%1 %2 minutes").arg(hours).arg(m);
    }
    function timeLeft(seconds) {
        if (!(seconds >= 0)) {
            return "";
        }
        if (seconds < 60) {
            return qsTr("Less than a minute left");
        }
        return qsTr("About %1 left").arg(duration(seconds));
    }
    // Minutes after midnight as "09:00".
    function minutes(m) {
        const h = Math.floor(m / 60);
        const mm = m % 60;
        return (h < 10 ? "0" : "") + h + ":" + (mm < 10 ? "0" : "") + mm;
    }
    // Days 1 (Monday) to 7 (Sunday) as "every day", "weekdays", "every Monday".
    function days(list) {
        const d = list.slice().sort();
        if (d.length === 7) {
            return qsTr("every day");
        }
        if (d.length === 0) {
            return qsTr("never");
        }
        if (d.join() === "1,2,3,4,5") {
            return qsTr("on weekdays");
        }
        if (d.join() === "6,7") {
            return qsTr("at weekends");
        }
        const names = d.map(n => Qt.locale().dayName(n % 7, Locale.LongFormat));
        if (names.length === 1) {
            return qsTr("every %1").arg(names[0]);
        }
        return qsTr("on %1 and %2").arg(names.slice(0, -1).join(", ")).arg(names[names.length - 1]);
    }
}
