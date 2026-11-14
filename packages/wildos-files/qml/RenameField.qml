import QtQuick
import WildOS.Files
import WildOS.UI as UI

// The in-place name editor. Enter or clicking away keeps the new name,
// Escape puts the old one back. With extensions hidden only the name is
// edited and the extension stays, as on Windows.
Rectangle {
    id: field

    required property string original
    property bool multiLine: false
    signal commit(string text)
    signal cancel()

    radius: 4
    color: UI.Style.windowOpaque
    border.width: 1
    border.color: UI.Style.accent
    implicitHeight: multiLine ? Math.min(input.contentHeight + 8, Metrics.fit(52, 12)) : Metrics.fit(26, 13)

    property bool done: false

    function finish(keep) {
        if (done) {
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
    // Down, Page Down) would reach the view behind, which moves the
    // selection, takes focus and ends the rename. As in Explorer they stop
    // here: only Enter, Escape, Tab or clicking away end it.
    Keys.onPressed: event => {
        if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab) {
            event.accepted = true;
        }
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: field.multiLine ? TextInput.AlignHCenter : TextInput.AlignLeft
        wrapMode: field.multiLine ? TextInput.WrapAnywhere : TextInput.NoWrap
        text: field.original
        color: UI.Style.inkField
        selectionColor: UI.Style.textSelection
        // The name keeps its colour when selected, so starting a rename does
        // not make it look brighter; the highlight shows the selection.
        selectedTextColor: UI.Style.inkField
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(UI.Style.px(field.multiLine ? 12 : 13))
        font.pixelSize: UI.Style.px(field.multiLine ? 12 : 13)
        clip: true
        selectByMouse: true
        Accessible.role: Accessible.EditableText
        Accessible.name: "New name"

        Component.onCompleted: {
            selectAll();
            forceActiveFocus();
        }
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
