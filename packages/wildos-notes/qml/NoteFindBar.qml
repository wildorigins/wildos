import QtQuick
import WildOS.UI as UI

// Find in the open note: a small bar at the top right of the note, in the
// menu glass, with how many matches there are and the arrows to step
// through them. Enter goes to the next, Shift+Enter the one before. The
// match is shown as the note's selection, so closing the bar leaves the
// cursor on it.
Rectangle {
    id: bar

    required property TextEdit edit
    required property Flickable flickable
    // { start, length } in the note's text.
    property var hits: []
    property int hitIndex: -1
    readonly property bool searching: field.activeFocus

    function start() {
        visible = true;
        field.forceActiveFocus();
        field.selectAll();
        search(true);
    }
    function stop() {
        visible = false;
        hits = [];
        hitIndex = -1;
    }
    function leave() {
        stop();
        edit.forceActiveFocus();
    }
    // With move, the first match from the cursor on is shown; without, the
    // matches are only counted again, as when the note is typed in.
    function search(move) {
        const found = [];
        if (field.text.length > 0) {
            const pattern = new RegExp(field.text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi");
            const text = edit.getText(0, edit.length);
            let match = pattern.exec(text);
            while (match !== null && match[0].length > 0) {
                found.push({ start: match.index, length: match[0].length });
                match = pattern.exec(text);
            }
        }
        hits = found;
        const first = found.findIndex(h => h.start >= edit.selectionStart);
        hitIndex = found.length === 0 ? -1 : first < 0 ? 0 : first;
        if (move) {
            show();
        }
    }
    function step(by) {
        if (hits.length === 0) {
            return;
        }
        hitIndex = (hitIndex + by + hits.length) % hits.length;
        show();
    }
    function show() {
        const hit = hits[hitIndex];
        if (hit === undefined) {
            return;
        }
        edit.select(hit.start, hit.start + hit.length);
        const line = edit.positionToRectangle(hit.start);
        const at = edit.mapToItem(flickable.contentItem, line.x, line.y);
        // Clear of this bar at the top, and of the bottom edge.
        if (at.y < flickable.contentY + 72 || at.y + line.height > flickable.contentY + flickable.height - 24) {
            flickable.contentY = Math.max(0, Math.min(flickable.contentHeight - flickable.height, at.y - flickable.height / 3));
        }
    }

    visible: false
    width: row.implicitWidth + 16
    height: 48
    radius: UI.Style.radiusMd
    color: UI.Style.glassMenu
    border.width: 1
    border.color: UI.Style.borderMenu

    Connections {
        target: bar.visible ? bar.edit : null
        function onTextChanged() {
            bar.search(false);
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4

        UI.SearchField {
            id: field
            width: 240
            height: 32
            placeholderText: qsTr("Find in this note")
            onTextChanged: bar.search(true)
            onEscapePressed: bar.leave()
            Keys.onReturnPressed: event => bar.step((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
            Keys.onEnterPressed: event => bar.step((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
        }

        UI.Label {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(72, implicitWidth)
            leftPadding: 6
            rightPadding: 4
            size: 12
            color: UI.Style.inkMuted
            text: field.text.length === 0 ? "" : bar.hits.length === 0 ? qsTr("No matches") : qsTr("%1 of %2").arg(bar.hitIndex + 1).arg(bar.hits.length)
        }

        UI.IconButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
            iconName: "chevron-up"
            iconSize: 14
            tip: qsTr("Previous match")
            enabled: bar.hits.length > 0
            onClicked: bar.step(-1)
        }
        UI.IconButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
            iconName: "chevron-down"
            iconSize: 14
            tip: qsTr("Next match")
            enabled: bar.hits.length > 0
            onClicked: bar.step(1)
        }
        UI.IconButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
            iconName: "close"
            iconSize: 13
            tip: qsTr("Close")
            onClicked: bar.leave()
        }
    }
}
