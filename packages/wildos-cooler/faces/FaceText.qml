import QtQuick

// A line of text placed the way the board places it: by its baseline, and
// either centred on a point or starting from it.
Text {
    property real cx: 320
    property real baseY: 0
    property bool centred: true
    property real size: 24
    property real spacing: 0

    x: centred ? cx - width / 2 : cx
    y: baseY - baselineOffset
    color: "#ffffff"
    font.family: "Albert Sans"
    font.pixelSize: size
    font.letterSpacing: spacing
    // Faces are drawn at 640 and shown at anything down to a thumbnail, so
    // the glyphs are drawn as curves rather than from a pixel-sized cache.
    renderType: Text.CurveRendering
}
