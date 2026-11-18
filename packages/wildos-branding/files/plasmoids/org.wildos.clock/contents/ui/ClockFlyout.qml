pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ksvg as KSvg
import org.kde.plasma.core as PlasmaCore

// The clock's flyout: the design's two glass cards, notifications above the
// calendar. Plasma's popup frame is the calendar card. The notifications card
// is a second glass window placed 12 px above it, which never takes keyboard
// focus, so clicking it cannot make the popup think it was left and close.
// Keyboard focus for both cards is therefore kept here, in the popup.
FocusScope {
    id: flyout

    required property var clock

    KSvg.FrameSvgItem {
        id: frame
        imagePath: "dialogs/background"
        visible: false
    }

    readonly property real edgeLeft: frame.margins.left
    readonly property real edgeRight: frame.margins.right
    readonly property real edgeTop: frame.margins.top
    readonly property real edgeBottom: frame.margins.bottom

    // The design pads the calendar 16 px at the top and 18 px at the sides and
    // foot, inside the 1px border; the 44 px header buttons add 6 px above.
    readonly property real calendarGlassHeight: 2 * Metrics.glassBorder + 16 - 6 + calendar.implicitHeight + 18
    readonly property real contentWidth: Metrics.cardWidth - edgeLeft - edgeRight
    readonly property real contentHeight: calendarGlassHeight - edgeTop - edgeBottom

    Layout.minimumWidth: contentWidth
    Layout.preferredWidth: contentWidth
    Layout.maximumWidth: contentWidth
    Layout.minimumHeight: contentHeight
    Layout.preferredHeight: contentHeight
    Layout.maximumHeight: contentHeight

    focus: true

    CalendarCard {
        id: calendar
        x: Metrics.glassBorder + 18 - flyout.edgeLeft
        y: Metrics.glassBorder + 16 - 6 - flyout.edgeTop
        width: Metrics.cardWidth - 2 * (Metrics.glassBorder + 18)
        today: flyout.clock.now
    }

    HoverHandler {
        onPointChanged: Nav.keyboard = false
    }

    function stops(): var {
        return notifications.stops().concat(calendar.stops());
    }

    function moveFocus(step: int): void {
        const list = stops();
        if (list.length === 0) {
            return;
        }
        let index = list.indexOf(Nav.current);
        index = index < 0 ? (step > 0 ? 0 : list.length - 1) : (index + step + list.length) % list.length;
        Nav.current = list[index];
        Nav.keyboard = true;
        notifications.reveal(Nav.current);
    }

    Keys.onPressed: event => {
        if (notifications.menuKey(event.key)) {
            event.accepted = true;
            return;
        }
        // The Menu key, on a notification or one of its buttons.
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            let target = Nav.keyboard ? Nav.current : null;
            while (target && !target.requestMenu) {
                target = target.parent;
            }
            if (target) {
                target.requestMenu();
            }
            event.accepted = true;
            return;
        }
        // On the calendar's grid the arrows move from day to day; Tab leaves.
        if (Nav.current === calendar.cells && calendar.handleKey(event.key)) {
            Nav.keyboard = true;
            event.accepted = true;
            return;
        }
        switch (event.key) {
        case Qt.Key_Tab:
        case Qt.Key_Down:
            moveFocus(1);
            break;
        case Qt.Key_Backtab:
        case Qt.Key_Up:
            moveFocus(-1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (Nav.current) {
                Nav.current.activate();
            }
            break;
        case Qt.Key_PageUp:
            calendar.step(-1);
            break;
        case Qt.Key_PageDown:
            calendar.step(1);
            break;
        case Qt.Key_Home:
            calendar.goToToday();
            break;
        case Qt.Key_Escape:
            // First back from the months or years to the month.
            if (calendar.mode !== "days") {
                calendar.mode = "days";
            } else {
                flyout.clock.expanded = false;
            }
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    // A button that disappears (a dismissed notification) must not keep the
    // keyboard's place.
    Connections {
        target: flyout.clock.history
        function onCountChanged() {
            if (Nav.current && flyout.stops().indexOf(Nav.current) < 0) {
                Nav.current = null;
            }
        }
    }

    Connections {
        target: flyout.clock
        function onExpandedChanged() {
            if (flyout.clock.expanded) {
                Nav.keyboard = false;
                Nav.current = null;
                calendar.goToToday();
                notifications.now = Date.now();
                flyout.forceActiveFocus();
                Qt.callLater(flyout.placeCard);
            }
        }
    }

    // The popup window Plasma made for this flyout; the notifications card
    // follows it.
    readonly property var popup: flyout.Window.window
    readonly property bool popupShown: clock.expanded && popup !== null && popup.visible

    function placeCard(): void {
        if (!popup) {
            return;
        }
        // Wild OS has no top panel, so the card may reach the screen's top edge,
        // less the design's 16 px.
        const screenTop = flyout.Screen.virtualY;
        notifications.maxHeight = Math.max(160, popup.y - Metrics.cardGap - (screenTop + 16)
            - 2 * (Metrics.glassBorder + 16));
        card.x = popup.x + Math.round((popup.width - card.width) / 2);
        card.y = popup.y - Metrics.cardGap - card.height;
    }

    Connections {
        target: flyout.popup
        function onXChanged() { flyout.placeCard(); }
        function onYChanged() { flyout.placeCard(); }
        function onWidthChanged() { flyout.placeCard(); }
        function onHeightChanged() { flyout.placeCard(); }
    }

    PlasmaCore.Dialog {
        id: card

        // A notification-type window is placed where it is told and is never
        // given keyboard focus, on both X11 and Wayland.
        type: PlasmaCore.Dialog.Notification
        flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        location: PlasmaCore.Types.Floating
        hideOnWindowDeactivate: false
        // The card belongs to the popup: the taskbar closes a pop-up on a
        // press in any window that is not the pop-up's own or one of its
        // children, and a press on this card would otherwise close the
        // flyout before the click arrived.
        transientParent: flyout.popup
        visible: flyout.popupShown
        onHeightChanged: flyout.placeCard()
        onVisibleChanged: {
            if (visible) {
                flyout.placeCard();
                settle.restart();
            }
        }

        // Plasma can still be moving the popup into place as it appears.
        property Timer settle: Timer {
            interval: 120
            onTriggered: flyout.placeCard()
        }

        mainItem: Item {
            // The design pads the card 16 px inside its 1px border.
            readonly property real pad: Metrics.glassBorder + 16
            width: Metrics.cardWidth - flyout.edgeLeft - flyout.edgeRight
            height: notifications.implicitHeight + 2 * pad - flyout.edgeTop - flyout.edgeBottom

            HoverHandler {
                onPointChanged: Nav.keyboard = false
            }

            NotificationsCard {
                id: notifications
                x: parent.pad - flyout.edgeLeft
                y: parent.pad - flyout.edgeTop
                width: Metrics.cardWidth - 2 * parent.pad
                clock: flyout.clock
            }

            // "2 min" ago labels move on while the card is open.
            Timer {
                interval: 30000
                repeat: true
                running: card.visible
                onTriggered: notifications.now = Date.now()
            }
        }
    }
}
