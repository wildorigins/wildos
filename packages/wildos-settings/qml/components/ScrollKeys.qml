import QtQuick
import QtQuick.Window
import WildOS.UI as UI

// Keyboard scrolling for a page: Page Up, Page Down, Home and End move the
// view, and a control the keyboard moves to beyond the edge is brought into
// view, as Windows does. Lives inside the Flickable it scrolls.
Item {
    id: keys

    property Flickable view
    readonly property real first: view ? view.originY - view.topMargin : 0
    readonly property real last: view ? Math.max(first, view.originY + view.contentHeight + view.bottomMargin - view.height) : 0
    // Where the view is, or is on its way to.
    readonly property real resting: glide.running ? glide.to : (view ? view.contentY : 0)

    visible: false

    function scrollTo(y) {
        const to = Math.max(first, Math.min(last, y));
        if (to === resting) {
            return;
        }
        glide.stop();
        glide.from = view.contentY;
        glide.to = to;
        glide.start();
    }

    // True when the key is one of the four, whether or not there was
    // anywhere left to go.
    function press(key) {
        const page = Math.max(40, view.height - 40);
        switch (key) {
        case Qt.Key_PageDown:
            scrollTo(resting + page);
            return true;
        case Qt.Key_PageUp:
            scrollTo(resting - page);
            return true;
        case Qt.Key_Home:
            scrollTo(first);
            return true;
        case Qt.Key_End:
            scrollTo(last);
            return true;
        }
        return false;
    }

    function handle(event) {
        const held = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier | Qt.MetaModifier);
        event.accepted = held === 0 && press(event.key);
    }

    // Only for a control the keyboard moved to: one that was clicked is
    // already under the pointer, and moving it would lose the click.
    function reveal() {
        const window = keys.Window.window;
        const item = window ? window.activeFocusItem : null;
        if (!item || !view) {
            return;
        }
        if (item.visualFocus !== true && item.focusReason !== Qt.TabFocusReason && item.focusReason !== Qt.BacktabFocusReason) {
            return;
        }
        for (let p = item; p !== view.contentItem; p = p.parent) {
            if (!p) {
                return;
            }
        }
        const margin = 24;
        const y = item.mapToItem(view.contentItem, 0, 0).y;
        if (y - margin < resting) {
            scrollTo(y - margin);
        } else if (y + item.height + margin > resting + view.height) {
            scrollTo(Math.min(y - margin, y + item.height + margin - view.height));
        }
    }

    Connections {
        target: keys.Window.window
        function onActiveFocusItemChanged() {
            Qt.callLater(keys.reveal);
        }
    }

    // A wheel or a drag takes over from a scroll still on its way.
    Connections {
        target: keys.view
        function onMovementStarted() {
            glide.stop();
        }
    }

    NumberAnimation {
        id: glide
        target: keys.view
        property: "contentY"
        duration: UI.Style.motionNormal
        easing.type: UI.Style.easeEnter
    }
}
