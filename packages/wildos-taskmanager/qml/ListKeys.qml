import QtQuick

// The keys every Task Manager table shares, as lists have them in Windows:
// Up, Down, Home, End, Page Up and Page Down move the selection, and typing
// the start of a name jumps to it. The page that owns the table is the
// `host` and answers for its rows with three functions: rowIndex() is the
// selected row or -1, selectRow(index) selects one, rowName(index) is what
// typing matches. A page whose table has rows that cannot be selected adds
// rowSelectable(index).
Item {
    id: keys

    property ListView view
    property Item host
    // The rows' right-click menu, if the table has one.
    property var rowMenu: null
    property real rowHeight: 40
    // Set by a click, so the keyboard's outline only shows once the
    // keyboard is what moved the selection.
    property bool byPointer: false
    readonly property bool shown: view !== null && view.activeFocus && !byPointer
    property string typed

    visible: false

    function selectable(index) {
        return host.rowSelectable === undefined || host.rowSelectable(index);
    }

    // The nearest row that can be selected, from `index` in the direction
    // of `step`.
    function go(index, step) {
        let i = index;
        while (i >= 0 && i < view.count && !selectable(i)) {
            i += step;
        }
        if (i < 0 || i >= view.count) {
            return;
        }
        host.selectRow(i);
        view.positionViewAtIndex(i, ListView.Contain);
    }

    function clicked() {
        byPointer = true;
        view.forceActiveFocus();
    }

    function type(event) {
        const letter = event.text.toLowerCase();
        if (event.text.length !== 1 || letter < " " || letter === "\u007f" || (letter === " " && typed.length === 0)) {
            return false;
        }
        // The same letter again steps to the next name that starts with it.
        const again = typed === letter;
        if (!again) {
            typed += letter;
        }
        const at = host.rowIndex();
        const start = again || typed.length === 1 ? at + 1 : Math.max(0, at);
        for (let n = 0; n < view.count; ++n) {
            const i = (start + n) % view.count;
            if (selectable(i) && host.rowName(i).toLowerCase().startsWith(typed)) {
                host.selectRow(i);
                view.positionViewAtIndex(i, ListView.Contain);
                break;
            }
        }
        forget.restart();
        return true;
    }

    function handle(event) {
        if (view.count === 0 || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) !== 0) {
            return;
        }
        const at = host.rowIndex();
        const page = Math.max(1, Math.floor(view.height / rowHeight) - 1);
        switch (event.key) {
        case Qt.Key_Up:
            go(at < 0 ? view.count - 1 : at - 1, -1);
            break;
        case Qt.Key_Down:
            go(at + 1, 1);
            break;
        case Qt.Key_Home:
            go(0, 1);
            break;
        case Qt.Key_End:
            go(view.count - 1, -1);
            break;
        case Qt.Key_PageUp:
            go(Math.max(0, Math.max(0, at) - page), 1);
            break;
        case Qt.Key_PageDown:
            go(Math.min(view.count - 1, Math.max(0, at) + page), -1);
            break;
        default:
            if (!type(event)) {
                return;
            }
        }
        byPointer = false;
        event.accepted = true;
    }

    Connections {
        target: keys.view
        function onActiveFocusChanged() {
            if (!keys.view.activeFocus) {
                // The keyboard comes back here when the row's menu closes,
                // and a menu opened with the mouse leaves no outline.
                if (!(keys.rowMenu && keys.rowMenu.visible)) {
                    keys.byPointer = false;
                }
            } else if (!keys.byPointer && keys.host.rowIndex() < 0) {
                keys.go(0, 1);
            }
        }
    }

    Timer {
        id: forget
        interval: 1000
        onTriggered: keys.typed = ""
    }
}
