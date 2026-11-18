// Dates and times in the regional format chosen in Settings > Language and
// region (LC_TIME, which Qt.locale() follows), with day and month names in
// the display language chosen in Settings (LANGUAGE), as Mail's Formats.js
// does, so a French taskbar says "lundi" whatever the regional format.
// Times also follow the
// 12 or 24-hour clock chosen in Settings > Date and time, through UI, the
// WildOS.UI import of the file that imports this one (a .import here would
// hide QtQuick's Locale from it).

function names() {
    const languages = Qt.locale().uiLanguages;
    const first = languages.length > 0 ? languages[0] : "";
    return first === "" || first === "C" || first.startsWith("en") ? Qt.locale("en_GB") : Qt.locale(first.replace("-", "_"));
}

// The formats are TimeFormat's properties, so a binding that calls this
// follows a change of clock in Settings straight away.
function time(when, seconds) {
    return Qt.locale().toString(when, seconds ? UI.TimeFormat.secondsFormat : UI.TimeFormat.shortFormat);
}

function shortDate(when) {
    return names().toString(when, Qt.locale().dateFormat(Locale.ShortFormat));
}

// Qt writes UK dates as "Monday, 9 November"; the design has no comma.
function longFormat() {
    return Qt.locale().dateFormat(Locale.LongFormat).replace(/^dddd, d MMMM/, "dddd d MMMM");
}

function longDate(when) {
    return names().toString(when, longFormat());
}

// The long date without its year: "Monday 9 November", "Monday, November 9".
function dayAndMonth(when) {
    return names().toString(when, longFormat().replace(/[,.\s]*y+[.年]?/, "").trim());
}

// 0 for Sunday to 6 for Saturday, as Date.getDay() numbers them.
function firstDayOfWeek() {
    return Qt.locale().firstDayOfWeek;
}
