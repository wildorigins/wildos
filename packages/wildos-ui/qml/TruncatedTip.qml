import QtQuick

// What a Label with `tipWhenTruncated` makes for itself: its whole text in
// a tooltip while the pointer rests on it and the text does not fit.
Item {
    id: tip

    required property Text label

    anchors.fill: parent

    HoverHandler {
        id: hover
    }

    Tooltip {
        text: tip.label.text
        visible: hover.hovered && tip.label.truncated
    }
}
