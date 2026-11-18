import QtQuick

// Text in Albert Sans at the design's UI size, scaled with the system text size.
// Set `size` to a design pixel size; it is scaled for you.
//
// `tipWhenTruncated` shows the whole text in a tooltip while the pointer
// rests on a label that had to be cut short. It is asked for label by
// label, so the many labels that never need it carry no hover handler.
Text {
    id: label

    property real size: 13
    property bool tipWhenTruncated: false
    property Item truncatedTip: null

    onTipWhenTruncatedChanged: {
        if (tipWhenTruncated && !truncatedTip) {
            // Loaded by name when asked for: Tooltip is itself made of a
            // Label, and the two files cannot name each other.
            truncatedTip = Qt.createComponent(Qt.resolvedUrl("TruncatedTip.qml")).createObject(label, { label: label });
        } else if (!tipWhenTruncated && truncatedTip) {
            truncatedTip.destroy();
            truncatedTip = null;
        }
    }

    color: Style.ink
    font.family: Style.fontFamily
    font.pixelSize: Style.px(size)
    // Read through a property of its own: a binding on font that reads
    // font.pixelSize runs again when it writes letterSpacing, which Qt
    // reports as a binding loop. This one only changes with the size.
    readonly property real trackedSize: font.pixelSize
    font.letterSpacing: Style.tracking(trackedSize)
    textFormat: Text.PlainText
    elide: Text.ElideRight
    maximumLineCount: wrapMode === Text.NoWrap ? 1 : 0
    verticalAlignment: Text.AlignVCenter
}
