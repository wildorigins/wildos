import QtQuick
import WildOS.UI as UI

// The name editor under an icon (F2). Enter or clicking away keeps the new
// name, Escape puts the old one back. With extensions hidden only the name
// is edited and the extension stays, as on Windows.
Rectangle {
    id: field

    property string original
    property bool active: false
    signal commit(string text)
    signal cancel()

    property bool done: false

    radius: 4
    color: UI.Style.windowOpaque
    border.width: 1
    border.color: UI.Style.accent
    height: Math.max(input.contentHeight, UI.Style.px(12) * 1.3) + 8

    onActiveChanged: {
        if (active) {
            done = false;
            input.text = original;
            input.selectAll();
            input.forceActiveFocus();
        }
    }

    function finish(keep) {
        if (done || !active) {
            return;
        }
        done = true;
        if (keep) {
            commit(input.text);
        } else {
            cancel();
        }
    }

    // Keys the text has no use for (Left at the start, Right at the end, Up,
    // Down, Page Down) would reach the desktop behind, which moves the
    // selection, takes focus and ends the rename. As in Explorer they stop
    // here: only Enter, Escape, Tab or clicking away end it.
    Keys.onPressed: event => {
        if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab) {
            event.accepted = true;
        }
    }

    TextInput {
        id: input
        x: 4
        y: 4
        width: parent.width - 8
        horizontalAlignment: TextInput.AlignHCenter
        wrapMode: TextInput.WrapAtWordBoundaryOrAnywhere
        color: UI.Style.inkField
        selectionColor: UI.Style.textSelection
        // The name keeps its colour when selected, so starting a rename does
        // not make it look brighter; the highlight shows the selection.
        selectedTextColor: UI.Style.inkField
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(UI.Style.px(12))
        font.pixelSize: UI.Style.px(12)
        selectByMouse: true
        Accessible.role: Accessible.EditableText
        Accessible.name: "New name"

        Keys.onReturnPressed: field.finish(true)
        Keys.onEnterPressed: field.finish(true)
        Keys.onEscapePressed: field.finish(false)
        Keys.onPressed: event => {
            // Characters a name cannot hold are refused as they are typed.
            if (event.text === "/") {
                event.accepted = true;
            } else if (nameMenu.isMenuKey(event)) {
                nameMenu.openAtCaret();
                event.accepted = true;
            }
        }
        // The text menu holds focus while it is open; the rename is not over.
        onActiveFocusChanged: if (!activeFocus && !nameMenu.opened) field.finish(true)

        UI.TextMenu {
            id: nameMenu
            onFocusLeft: field.finish(true)
        }
    }
}
