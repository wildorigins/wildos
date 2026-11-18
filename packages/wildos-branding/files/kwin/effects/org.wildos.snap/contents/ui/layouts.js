.pragma library

// Zones are fractions of the screen's work area (the part not covered by the
// taskbar), as [x, y, width, height]. The four layouts of Snap.dc, drawn in
// the picker in this order, two to a row; keyboard focus moves through the
// zones in the order they are listed.
var layouts = [
    {
        name: qsTr("Left half and right half"),
        zones: [[0, 0, 1 / 2, 1], [1 / 2, 0, 1 / 2, 1]],
        labels: [qsTr("Left half"), qsTr("Right half")]
    },
    {
        name: qsTr("Two thirds and one third"),
        zones: [[0, 0, 2 / 3, 1], [2 / 3, 0, 1 / 3, 1]],
        labels: [qsTr("Left two thirds"), qsTr("Right third")]
    },
    {
        name: qsTr("Three columns"),
        zones: [[0, 0, 1 / 3, 1], [1 / 3, 0, 1 / 3, 1], [2 / 3, 0, 1 / 3, 1]],
        labels: [qsTr("Left third"), qsTr("Middle third"), qsTr("Right third")]
    },
    {
        name: qsTr("Four quarters"),
        zones: [[0, 0, 1 / 2, 1 / 2], [1 / 2, 0, 1 / 2, 1 / 2], [0, 1 / 2, 1 / 2, 1 / 2], [1 / 2, 1 / 2, 1 / 2, 1 / 2]],
        labels: [qsTr("Top left quarter"), qsTr("Top right quarter"), qsTr("Bottom left quarter"), qsTr("Bottom right quarter")]
    }
];

var columns = 2;

// Halves and quarters go through KWin's own quick tiling, which remembers the
// window's previous size and gives it back when the window is dragged out.
// Every other zone is applied as a plain geometry.
function quickTileSlot(zone) {
    var key = zone.map(function (v) { return Math.round(v * 100); }).join(",");
    return {
        "0,0,50,100": "slotWindowQuickTileLeft",
        "50,0,50,100": "slotWindowQuickTileRight",
        "0,0,50,50": "slotWindowQuickTileTopLeft",
        "50,0,50,50": "slotWindowQuickTileTopRight",
        "0,50,50,50": "slotWindowQuickTileBottomLeft",
        "50,50,50,50": "slotWindowQuickTileBottomRight"
    }[key] || "";
}

function zoneRect(zone, area) {
    var left = Math.round(area.x + zone[0] * area.width);
    var top = Math.round(area.y + zone[1] * area.height);
    var right = Math.round(area.x + (zone[0] + zone[2]) * area.width);
    var bottom = Math.round(area.y + (zone[1] + zone[3]) * area.height);
    return Qt.rect(left, top, right - left, bottom - top);
}

function sameRect(a, b) {
    return Math.abs(a.x - b.x) <= 2 && Math.abs(a.y - b.y) <= 2
        && Math.abs(a.width - b.width) <= 2 && Math.abs(a.height - b.height) <= 2;
}

// Where the window's maximise button is, in global coordinates. Scripts
// cannot see decoration buttons, so this works from what they can see: the
// title bar height (frame top to client top). Wild OS puts minimise, maximise
// and close on the right (ButtonsOnRight=IAX), so maximise is second from the
// right. Three decorations are recognised, in this order:
//
//  - The Wild OS Aurorae theme, using its own metrics (passed in as `theme`,
//    the effect's configuration), when the title bar is the height the theme
//    produces. Aurorae lines the right button group up with the frame's right
//    edge less TitleEdgeRight and scales buttons by its ButtonSize factor.
//  - Breeze, repeating Breeze 6.3's arithmetic, when the height fits it.
//  - Anything else: square buttons as tall as the title bar, flush right.

var auroraeFactors = [1.0, 1.2, 1.4, 1.6, 1.8, 2.0];

function auroraeRect(frame, titleHeight, maximizeMode, theme) {
    if (!theme || !(theme.TitleHeight > 0)) {
        return null;
    }
    // Aurorae switches to its maximised metrics only when fully maximised.
    var maximised = maximizeMode === 3;
    var edgeTop = maximised ? theme.TitleEdgeTopMaximized : theme.TitleEdgeTop;
    var edgeBottom = maximised ? theme.TitleEdgeBottomMaximized : theme.TitleEdgeBottom;
    var edgeRight = maximised ? theme.TitleEdgeRightMaximized : theme.TitleEdgeRight;
    var marginTop = maximised ? theme.ButtonMarginTopMaximized : theme.ButtonMarginTop;
    // Factors below 1 leave the title bar height alone, so they can only
    // come from configuration; larger ones show in the height.
    var candidates = theme.ButtonSizeFactor < 1 ? [theme.ButtonSizeFactor] : auroraeFactors;
    for (var i = 0; i < candidates.length; ++i) {
        var f = candidates[i];
        var expected = Math.max(theme.TitleHeight, theme.ButtonHeight * f + marginTop) + edgeTop + edgeBottom;
        if (Math.abs(expected - titleHeight) <= 1) {
            var close = theme.ButtonWidthClose * f;
            var width = theme.ButtonWidthMaximizeRestore * f;
            var right = frame.x + frame.width - edgeRight - close - theme.ButtonSpacing * f;
            return Qt.rect(Math.round(right - width), Math.round(frame.y + edgeTop + marginTop),
                           Math.round(width), Math.round(theme.ButtonHeight * f));
        }
    }
    return null;
}

// Breeze buttons are square, twice the decoration grid unit, with twice the
// small spacing (a quarter of the unit, at least 2) between them and at the
// right edge. The title bar is the button height plus four small spacings,
// so the unit is recovered from the height whatever the title font or scale.
function breezeRect(frame, titleHeight, maximizeMode) {
    // Breeze drops the top margin whenever the window touches the top edge
    // through vertical maximising.
    var maximised = (maximizeMode & 1) !== 0;
    for (var unit = 4; unit <= 64; unit += 2) {
        var small = Math.max(2, Math.floor(unit / 4));
        if (Math.abs(2 * unit + 4 * small - titleHeight) <= 1) {
            var size = unit * 2;
            var top = maximised ? frame.y : frame.y + small * 2;
            var right = frame.x + frame.width - small * 2 - size - small * 2;
            return Qt.rect(right - size, top, size, size);
        }
    }
    return null;
}

function maximiseButtonRect(frame, titleHeight, maximizeMode, theme) {
    if (titleHeight < 16) {
        return Qt.rect(0, 0, 0, 0);
    }
    var rect = auroraeRect(frame, titleHeight, maximizeMode, theme) || breezeRect(frame, titleHeight, maximizeMode);
    if (rect) {
        return rect;
    }
    var side = Math.round(titleHeight);
    return Qt.rect(frame.x + frame.width - 2 * side, frame.y, side, side);
}
