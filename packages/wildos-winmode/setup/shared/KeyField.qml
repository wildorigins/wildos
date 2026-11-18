import QtQuick
import WildOS.UI as UI

// The 25-character product key, in capitals with a dash after every five
// however it is typed or pasted, as Windows' own key box does.
UI.TextField {
    id: field

    // The text after the last edit, to tell when an edit removed nothing
    // but a dash.
    property string shaped: ""
    property bool forwardDelete: false

    placeholderText: "XXXXX-XXXXX-XXXXX-XXXXX-XXXXX"
    font.letterSpacing: 0.5
    inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
    // Room for a pasted key with spaces round its dashes; shaping cuts it
    // to 25 characters.
    maximumLength: 64
    // Only once all 25 are in (29 with the dashes), not while typing.
    error: text.length >= 29 && !Setup.keyShapeValid(text)
    Accessible.name: qsTr("Windows product key")
    onEditingFinished: if (Setup.keyShapeValid(text)) text = Setup.normaliseKey(text)

    Keys.onDeletePressed: event => {
        forwardDelete = true;
        event.accepted = false;
        // The edit happens during this key press, so a Delete that removed
        // nothing must not count for a later Backspace.
        Qt.callLater(() => forwardDelete = false);
    }

    onTextEdited: {
        let typed = text;
        let cursor = cursorPosition;
        if (typed.length === shaped.length - 1) {
            let i = 0;
            while (i < typed.length && typed[i] === shaped[i]) {
                i++;
            }
            // The dash would only come straight back, so the character
            // beside it goes instead.
            if (shaped[i] === "-" && shaped.slice(i + 1) === typed.slice(i)) {
                if (forwardDelete) {
                    typed = typed.slice(0, i) + typed.slice(i + 1);
                } else if (i > 0) {
                    typed = typed.slice(0, i - 1) + typed.slice(i);
                    cursor = i - 1;
                }
            }
        }
        forwardDelete = false;
        const before = Math.min(25, typed.slice(0, cursor).replace(/[^0-9a-z]/gi, "").length);
        const next = typed.toUpperCase().replace(/[^0-9A-Z]/g, "").slice(0, 25).replace(/(.{5})(?=.)/g, "$1-");
        shaped = next;
        if (next !== text) {
            text = next;
        }
        cursorPosition = before + Math.floor(Math.max(before - 1, 0) / 5);
    }
}
