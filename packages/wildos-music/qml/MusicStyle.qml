pragma Singleton

import QtQuick
import WildOS.UI as UI

// The few values the board gives Music that the system tokens do not have.
QtObject {
    // A liked song's heart (AppMusic.dc #ff7aa8, AppMusicLight.dc #9e4b68).
    readonly property color liked: UI.Style.contrast ? "#ff9cc4" : UI.Style.dark ? "#ff7aa8" : "#9e4b68"
    // The playing song's title and bars: the accent as text, the one place
    // text takes it, since it marks what is playing.
    readonly property color playingInk: UI.Style.dark ? UI.Style.accent : UI.Style.accentText
    // The player bar and the side column sit on a neutral shade of the
    // window glass, as the board draws them.
    readonly property color barShade: UI.Style.contrast ? "#000000" : Qt.rgba(0, 0, 0, UI.Style.dark ? 0.22 : 0.11)
    readonly property color sideShade: UI.Style.contrast ? "#000000" : Qt.rgba(0, 0, 0, UI.Style.dark ? 0.12 : 0.06)
    // The seek and volume bars: a faint track with a fill in the ink of
    // the scheme, white on dark and near black on light, where a white
    // fill would be lost against the pale window. The big round play
    // button is white with a near black glyph in both schemes.
    readonly property color barTrack: UI.Style.contrast ? UI.Style.contrastDivider : UI.Style.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.126)
    readonly property color barFill: UI.Style.dark ? UI.Style.ink : UI.Style.iconInk
    readonly property color playDisc: "#ffffff"
    readonly property color playGlyph: "#111319"
    // Art that is missing: a neutral tile, never a made-up colour.
    readonly property color noCover: UI.Style.surfaceTile

    function time(ms) {
        if (!(ms > 0)) {
            return "0:00";
        }
        const total = Math.floor(ms / 1000);
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const ss = (s < 10 ? "0" : "") + s;
        return h > 0 ? h + ":" + (m < 10 ? "0" : "") + m + ":" + ss : m + ":" + ss;
    }
    // "47 min", "2 hr 5 min".
    function length(ms) {
        const minutes = Math.round(ms / 60000);
        if (minutes < 60) {
            return qsTr("%1 min").arg(minutes);
        }
        const h = Math.floor(minutes / 60);
        const m = minutes % 60;
        return m === 0 ? qsTr("%1 hr").arg(h) : qsTr("%1 hr %2 min").arg(h).arg(m);
    }
    function songs(count) {
        return count === 1 ? qsTr("1 song") : qsTr("%1 songs").arg(Number(count).toLocaleString(Qt.locale(), "f", 0));
    }
    function count(n) {
        return Number(n).toLocaleString(Qt.locale(), "f", 0);
    }
}
