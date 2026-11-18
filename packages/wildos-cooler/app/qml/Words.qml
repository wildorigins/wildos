import QtQuick
import WildOS.UI as UI

// A WildOS.UI Label set in the line boxes the Cooler boards were laid out
// with. The boards are web pages, where a line of text is a little taller
// than the one Qt gives the same font, and every line is centred in its
// box; a page built from plain labels ends up a few pixels short at every
// line and drifts from the board as it goes down. This keeps each line the
// board's height with the words on the board's baseline.
//
// `line` is the board's line-height as a multiple of the size (1.5 for the
// wrapped paragraphs); 0 is the browser's own, the font's ascent and
// descent each rounded to a whole pixel.
UI.Label {
    id: words

    property real line: 0

    readonly property real ascentBox: Math.round(0.95 * size * UI.Style.textScale)
    readonly property real naturalBox: ascentBox + Math.round(0.25 * size * UI.Style.textScale)
    readonly property real lineBox: line > 0 ? size * UI.Style.textScale * line : naturalBox

    FontMetrics {
        id: metrics
        font: words.font
    }

    lineHeightMode: Text.FixedHeight
    lineHeight: lineBox
    topPadding: Math.round((lineBox - naturalBox) / 2 + ascentBox - metrics.ascent)
    // Qt gives a line shorter than the font's own height (rounded up) that
    // height all the same.
    bottomPadding: Math.min(0, lineBox - Math.ceil(metrics.height)) - topPadding
}
