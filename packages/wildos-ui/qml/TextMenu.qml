import QtQuick

// The right-click menu for text you're typing (MenusApps.dc "Text you're
// typing"): Undo, Cut, Copy, Paste, Select all and Emoji, with their
// shortcuts. TextField and TextArea carry one; an app's own TextInput or
// TextEdit gets the same menu by declaring `UI.TextMenu {}` inside it, and
// calls open() from its Menu key handler. A handler rather than an item, so
// it receives the clicks on the field it is declared in.
TapHandler {
    id: handler

    readonly property Item field: parent

    property Menu menu: null
    // For an editor that keeps an undo of its own (Notes holds its line
    // spacing pass off while stepping back); the field's undo otherwise.
    property var undoAction: null
    // The open menu holds the field's focus, so a field that ends its
    // editing when focus leaves (a rename, an address bar) waits while
    // this is true. A click elsewhere that closes the menu leaves focus
    // there instead of handing it back, which focusLeft reports.
    readonly property bool opened: menu !== null && menu.visible
    signal focusLeft()

    // A field with an enabled NoPaste in it (Wild OS's own password boxes).
    function pasteBlocked(f): bool {
        for (let i = 0; i < f.data.length; ++i) {
            if (f.data[i] instanceof NoPaste && f.data[i].enabled) {
                return true;
            }
        }
        return false;
    }

    // A field with an enabled SecretCopy in it (a password shown as text).
    function secretCopy(f) {
        for (let i = 0; i < f.data.length; ++i) {
            if (f.data[i] instanceof SecretCopy && f.data[i].enabled) {
                return f.data[i];
            }
        }
        return null;
    }

    function open(px: real, py: real): void {
        const f = field;
        if (!menu) {
            menu = menuComponent.createObject(f);
        }
        // TextEdit and TextArea have no echoMode; undefined is never a password.
        const secret = f.echoMode === TextInput.Password || f.echoMode === TextInput.PasswordEchoOnEdit;
        const editable = !f.readOnly;
        const marked = secretCopy(f);
        const entries = [];
        if (editable) {
            entries.push({ text: "Undo", icon: "undo", shortcut: "Ctrl+Z", enabled: f.canUndo, run: () => handler.undoAction ? handler.undoAction() : f.undo() },
                { separator: true });
        }
        if (editable && !secret) {
            entries.push({ text: "Cut", icon: "menu-cut", shortcut: "Ctrl+X", enabled: f.selectedText.length > 0, run: () => marked ? marked.cut() : f.cut() });
        }
        if (!secret) {
            entries.push({ text: "Copy", icon: "menu-copy", shortcut: "Ctrl+C", enabled: f.selectedText.length > 0, run: () => marked ? marked.copy() : f.copy() });
        }
        if (editable && !pasteBlocked(f)) {
            entries.push({ text: "Paste", icon: "menu-paste", shortcut: "Ctrl+V", enabled: f.canPaste, run: () => f.paste() });
        }
        if (entries.length > 0 && !entries[entries.length - 1].separator) {
            entries.push({ separator: true });
        }
        entries.push({ text: "Select all", icon: "select-all", shortcut: "Ctrl+A", enabled: f.length > 0, run: () => f.selectAll() });
        if (editable && !secret && System.canRun("wildos-clipboard")) {
            entries.push({ text: "Emoji", icon: "emoji", shortcut: "Win+.", run: () => System.run("wildos-clipboard", ["--emoji"]) });
        }
        f.forceActiveFocus(Qt.PopupFocusReason);
        menu.entries = entries;
        menu.popup(f, px, py);
    }

    // For the Menu key and Shift+F10, which open it at the caret.
    function openAtCaret(): void {
        const caret = field.cursorRectangle;
        open(caret.x, caret.y + caret.height);
    }

    function isMenuKey(event) {
        return event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier));
    }

    enabled: field !== null && field.enabled
    acceptedButtons: Qt.RightButton
    onTapped: eventPoint => open(eventPoint.position.x, eventPoint.position.y)

    property Connections closing: Connections {
        target: handler.menu
        function onClosed() {
            const f = handler.field;
            if (!f || f.activeFocus) {
                return;
            }
            // Qt hands focus back to the window rather than the field when
            // the field sits in a focus scope (a Loader, a dialog), which
            // would leave a rename open with nowhere to type. Focus taken
            // by something else means a click went elsewhere.
            const w = f.Window.window;
            const now = w ? w.activeFocusItem : null;
            if (!now || now === w.contentItem) {
                f.forceActiveFocus(Qt.PopupFocusReason);
            } else {
                handler.focusLeft();
            }
        }
    }

    property Component menuComponent: Component {
        Menu {
            minimumWidth: 236
        }
    }
}
